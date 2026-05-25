/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SPARTAN_CLOUDS
#define SPARTAN_CLOUDS

//= includes ============
#include "../common.hlsl"
//=======================

// =====================================================================
// nubis-style volumetric clouds, baked into the equirectangular sky panorama
// reference: andrew schneider, the real-time volumetric cloudscapes of horizon zero dawn
// =====================================================================

// shared planet parameters with skysphere.hlsl, kept local to avoid include-order coupling
static const float cloud_earth_radius     = 6360e3;
static const float3 cloud_earth_center    = float3(0.0, -cloud_earth_radius, 0.0);

// cumulus layer
static const float cumulus_bottom_alt     = 1500.0;
static const float cumulus_top_alt        = 4000.0;
static const float cumulus_thickness      = cumulus_top_alt - cumulus_bottom_alt;
static const float cumulus_shape_scale    = 1.0 / 4500.0;   // one tile of the noise volume covers 4.5 km horizontally
static const float cumulus_detail_scale   = 1.0 / 2200.0;   // detail features now ~370m at the highest surviving octave, well above the 40m step nyquist
static const float cumulus_coverage_scale = 1.0 / 26000.0;  // weather map domain, pushes tile repeats out toward the horizon
static const float cumulus_coverage       = 0.40;           // 0 = no clouds, 1 = overcast, fair weather sits around 0.30 - 0.45
static const float cumulus_density_mul    = 0.80;
static const float cumulus_sigma_t        = 0.045;          // base extinction coefficient per meter

// domain warp, breaks up the visible grid alignment of the noise volume so cloud puffs
// do not appear in lanes and neighbouring puffs do not share shapes. the shape warp varies
// on a scale ~5x slower than the shape tile, so whole puffs get translated organically
// without any internal shearing that would stretch them into strips
static const float cumulus_warp_scale         = 1.0 / 45000.0;
static const float cumulus_warp_amplitude     = 7000.0;
static const float cumulus_shape_warp_scale   = 1.0 / 22000.0;
static const float cumulus_shape_warp         = 1700.0;

// per-cloud altitude offset, breaks up the visual sense of a uniform invisible dome that
// all clouds hug. faster noise scale than the weather domain so neighbouring clouds within
// a single weather region still get different base altitudes, otherwise each cluster reads
// as a flat plate even when the cluster-to-cluster variation is large
static const float cumulus_base_variation     = 1000.0;
static const float cumulus_base_var_scale     = 1.0 / 14000.0;
static const float cumulus_shell_padding      = 1200.0;

// cirrus layer
static const float cirrus_bottom_alt      = 6500.0;
static const float cirrus_top_alt         = 8000.0;
static const float cirrus_thickness       = cirrus_top_alt - cirrus_bottom_alt;
static const float cirrus_noise_scale     = 1.0 / 12000.0;
static const float cirrus_coverage        = 0.28;
static const float cirrus_density_mul     = 0.05;
static const float cirrus_sigma_t         = 0.03;
static const float3 cirrus_streak_axis    = float3(1.0, 0.0, 0.0); // direction of the wispy streaks

// constant wind offsets, drift the noise sampling so two skybox bakes show different cloudscapes
static const float3 cumulus_wind_offset   = float3(1234.0, 0.0,  567.0);
static const float3 cirrus_wind_offset    = float3(4321.0, 0.0, 8765.0);

// runtime wind drift multipliers, the cloud field is translated by -wind*time*mul over the
// horizontal noise lookups, so when the world wind vector is non zero clouds appear to flow
// along it. cumulus moves at the surface wind speed scaled by the cumulus multiplier, cirrus
// drifts faster than cumulus to match the high-altitude jet stream typically being several
// times stronger than ground level wind. only the noise sampling is translated, the sample
// altitude relative to the planet is unchanged so the cloud layer stays parked at its base.
// multipliers are tuned for the calm ~3 m/s ground wind the scenes ship with, so the effective
// drift lands at ~24 m/s for cumulus and ~75 m/s for cirrus which is fast enough to register
// as obvious motion within a few seconds of looking at the sky on a kilometer-scale cloud
static const float cumulus_wind_speed_mul = 8.0;
static const float cirrus_wind_speed_mul  = 25.0;

// per-layer feature evolution rate in meters per second, applied as a slow drift along the
// vertical axis of the cloud noise volume. the volume is tileable so the slide wraps cleanly
// and shapes morph in place over time, decoupled from the horizontal wind translation. the
// y axis is safe because cloud_weather and cloud_base_offset force uvw.y to 0.5, so this only
// affects the 3d shape, detail and warp lookups, not the coverage map or per-cloud altitude.
// rate is divided by the per-layer noise tile size when sampled, so the cumulus shape (4.5 km
// tile) cycles in ~2-3 min and detail (2.2 km tile) cycles in ~1 min at the value below,
// cirrus is intentionally much slower because real cirrus shapes persist for tens of minutes
static const float cumulus_evolve_rate    = 30.0;
static const float cirrus_evolve_rate     = 10.0;

// lighting
static const float3 cloud_ambient_bottom  = float3(0.18, 0.22, 0.30); // cool dark base
static const float3 cloud_ambient_top     = float3(0.95, 0.95, 0.99); // bright lit top
static const float  cloud_ambient_factor  = 0.15;   // ambient strength relative to sun luminance
static const float  cloud_albedo          = 0.97;   // single scattering albedo, near 1 for water clouds
static const float  cloud_sun_step_base   = 30.0;   // first sun-march step length, finer to avoid noisy self-shadow
static const float  cloud_sun_step_growth = 2.0;    // geometric growth between sun samples

// night lighting, faint cool blue glow so cumulus do not go pitch black after sunset. the
// moon tint is multiplied into the moon-dir transmittance lookup, the airglow floor models
// scattered starlight and atmospheric chemiluminescence and is present even without a moon
static const float3 cloud_moon_tint       = float3(0.0006, 0.0009, 0.0014);
static const float3 cloud_night_floor     = float3(0.0018, 0.0028, 0.0050);

// aerial perspective, atmospheric extinction along the camera-to-sample ray. matches a
// roughly 30 km clear-day visibility, so distant clouds fade into the haze instead of
// stacking up along grazing rays as a thick ring at the horizon
static const float  cloud_aerial_falloff  = 1.0e-4;

// raymarch sample budget. the skysphere bake runs every frame on the async compute queue
// to animate the clouds and the total skysphere pass must stay under ~5 ms, so the view
// march is sized aggressively, 80 steps at 125 m each covers the same 10 km range with
// 5/8 of the previous per-ray cost. the coarser per-ray density is masked by the 1/4
// partial dispatch in skysphere.hlsl, four interleaved phases each marching at slightly
// different angles average out into a stable image after a handful of frames
static const int    cumulus_view_steps    = 80;
static const int    cirrus_view_steps     = 24;
static const int    cumulus_sun_steps     = 8;

// =====================================================================
// noise generation (used by the CLOUD_NOISE compute kernel)
// =====================================================================

// integer-mix hash, stable and well distributed across the cell grid we use for noise
uint cloud_hash_uint(uint x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

uint cloud_hash3_to_uint(int3 c, int wrap)
{
    uint x = uint(((c.x % wrap) + wrap) % wrap);
    uint y = uint(((c.y % wrap) + wrap) % wrap);
    uint z = uint(((c.z % wrap) + wrap) % wrap);
    return cloud_hash_uint(x + cloud_hash_uint(y * 19349663u + cloud_hash_uint(z * 83492791u)));
}

// tileable 3d worley noise, returns 1 - distance_to_nearest so high values are inside cells
float cloud_worley_3d(float3 p, int cells)
{
    float3 scaled = p * cells;
    int3   ci     = int3(floor(scaled));
    float3 cf     = scaled - float3(ci);
    
    float min_d2 = 4.0;
    [unroll]
    for (int z = -1; z <= 1; z++)
    {
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            [unroll]
            for (int x = -1; x <= 1; x++)
            {
                int3 ofs = int3(x, y, z);
                uint h   = cloud_hash3_to_uint(ci + ofs, cells);
                float3 jitter = float3(
                    float((h >>  0) & 0xffu) / 255.0,
                    float((h >>  8) & 0xffu) / 255.0,
                    float((h >> 16) & 0xffu) / 255.0);
                float3 d = float3(ofs) + jitter - cf;
                min_d2   = min(min_d2, dot(d, d));
            }
        }
    }
    return saturate(1.0 - sqrt(min_d2));
}

// fractional brownian motion of worley noise, three octaves with halving amplitude
float cloud_worley_fbm(float3 p, int base_cells)
{
    float v  = cloud_worley_3d(p, base_cells)      * 0.625;
    v       += cloud_worley_3d(p, base_cells * 2)  * 0.250;
    v       += cloud_worley_3d(p, base_cells * 4)  * 0.125;
    return saturate(v);
}

// tileable 3d gradient noise on a cell grid, hash picks a unit direction per corner
float3 cloud_grad3(uint h)
{
    h &= 15u;
    float3 g;
    g.x = (h < 8u) ? 1.0 : -1.0;
    g.y = (((h >> 1) & 1u) != 0u) ? 1.0 : -1.0;
    g.z = (((h >> 2) & 1u) != 0u) ? 1.0 : -1.0;
    return g;
}

float cloud_perlin_3d(float3 p, int cells)
{
    float3 scaled = p * cells;
    int3   ci     = int3(floor(scaled));
    float3 cf     = scaled - float3(ci);
    float3 u      = cf * cf * cf * (cf * (cf * 6.0 - 15.0) + 10.0);
    
    float n[8];
    [unroll]
    for (int k = 0; k < 8; k++)
    {
        int3 ofs = int3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        uint h   = cloud_hash3_to_uint(ci + ofs, cells);
        float3 d = cf - float3(ofs);
        n[k]     = dot(cloud_grad3(h), d);
    }
    
    float nx0 = lerp(n[0], n[1], u.x);
    float nx1 = lerp(n[2], n[3], u.x);
    float nx2 = lerp(n[4], n[5], u.x);
    float nx3 = lerp(n[6], n[7], u.x);
    float ny0 = lerp(nx0, nx1, u.y);
    float ny1 = lerp(nx2, nx3, u.y);
    return saturate(lerp(ny0, ny1, u.z) * 0.5 + 0.5);
}

float cloud_perlin_fbm(float3 p, int base_cells)
{
    float v  = cloud_perlin_3d(p, base_cells)      * 0.5;
    v       += cloud_perlin_3d(p, base_cells * 2)  * 0.25;
    v       += cloud_perlin_3d(p, base_cells * 4)  * 0.125;
    v       += cloud_perlin_3d(p, base_cells * 8)  * 0.0625;
    return saturate(v / 0.9375);
}

// schneider remap, expands the range of a value
float cloud_remap(float v, float old_min, float old_max, float new_min, float new_max)
{
    return new_min + saturate((v - old_min) / max(old_max - old_min, 1e-6)) * (new_max - new_min);
}

// =====================================================================
// noise sampling at run time
// =====================================================================

// horizontal drift applied to the cloud noise sampling positions. negative wind*time so a
// positive wind vector translates the cloud field in the +wind direction over time. the
// returned offset is meant to be added to the position before any noise lookup, never to a
// position used for height or shell intersection
float3 cloud_wind_drift(float speed_mul)
{
    float3 wind = buffer_frame.wind * speed_mul;
    float  t    = (float)buffer_frame.time;
    return -wind * t;
}

// pure vertical drift through the cloud noise volume, used to morph cloud shapes in place
// independently of horizontal wind translation. the y axis is chosen so the offset never
// leaks into the 2d coverage lookups, which collapse their y to 0.5 internally, this keeps
// cloud regions stable on the sky while individual puffs billow and reshape inside them
float3 cloud_evolve_offset(float rate)
{
    return float3(0.0, rate * (float)buffer_frame.time, 0.0);
}

// the 3d cloud noise volume is bound at the tex3d slot during the skysphere pass
// channels: r = low-freq perlin-worley, g = worley fbm mid, b = worley fbm high, a = high-freq perlin
float4 cloud_sample_noise(Texture3D noise, SamplerState samp, float3 uvw)
{
    return noise.SampleLevel(samp, frac(uvw), 0);
}

// low-frequency domain warp, returns a meter-space offset that we add to the sample position
// before reading the cloud noise. the warp itself tiles at a much larger scale than the
// thing it warps, so the combined pattern has an effective period far beyond the visible horizon
float3 cloud_domain_warp(Texture3D noise, SamplerState samp, float3 pos, float scale, float amplitude)
{
    float3 uvw = pos * scale + 0.731;
    float4 n   = cloud_sample_noise(noise, samp, uvw);
    float3 v   = float3(n.r, n.b, n.g) * 2.0 - 1.0;
    return v * amplitude;
}

// per-cloud altitude offset in meters, sampled from a slow horizontal noise so each cloud
// region has its own base altitude. without this all cumulus end up parked on the same
// invisible ceiling at cumulus_bottom_alt which reads as an unnatural dome
float cloud_base_offset(Texture3D noise, SamplerState samp, float3 pos)
{
    float3 uvw = pos * cumulus_base_var_scale + 0.781;
    uvw.y      = 0.5;
    float4 n   = cloud_sample_noise(noise, samp, uvw);
    return (n.g * 2.0 - 1.0) * cumulus_base_variation;
}

// =====================================================================
// shape / density
// =====================================================================

// cumulus vertical profile, flat-ish bottom and rounded top, mirrors the schneider cumulus curve
// the bottom ramp extends well below zero so wispy tendrils have room to hang under the
// nominal base, this is what visually breaks the perceived flat floor of each cloud, the top
// fades smoothly over the upper half so the silhouette has continuously curving tops
float cloud_height_profile_cumulus(float h_norm)
{
    float bottom = smoothstep(-0.12, 0.22, h_norm);
    float top    = smoothstep(1.00, 0.55, h_norm);
    return saturate(bottom * top);
}

// cirrus vertical profile, soft hump centered in the layer
float cloud_height_profile_cirrus(float h_norm)
{
    return sin(saturate(h_norm) * PI);
}

// 2d procedural weather term, low-frequency horizontal coverage, drives where cumulus exists
// cumulus_coverage directly controls the fraction of horizontal area that has cloud, the
// (1 - coverage) threshold carves large clear regions instead of just biasing toward more cloud
// the position is domain-warped before sampling so the underlying noise tile grid does not
// appear as lanes of identical clumps across the sky, and a second much larger octave is
// blended in so clouds cluster into organic regions instead of an even sprinkle
float cloud_weather(Texture3D noise, SamplerState samp, float3 pos)
{
    // drift the coverage map along the world wind direction so cloud regions translate over
    // time instead of being parked over the same horizontal patches forever
    float3 pos_d = pos + cloud_wind_drift(cumulus_wind_speed_mul);
    float3 warp  = cloud_domain_warp(noise, samp, pos_d, cumulus_warp_scale, cumulus_warp_amplitude);
    float3 wpos  = pos_d + warp;
    
    float3 uvw_a = wpos * cumulus_coverage_scale;
    uvw_a.y      = 0.5;
    float4 na    = cloud_sample_noise(noise, samp, uvw_a + 0.123);
    float wa     = na.r * 0.7 + na.g * 0.3;
    
    // large-scale modulator, pushes some areas toward cloudy, others toward clear, on a scale
    // much bigger than the weather tile itself so visible repeats fall beyond the horizon
    float3 uvw_b = wpos * (cumulus_coverage_scale * 0.35) + 0.781;
    uvw_b.y      = 0.5;
    float4 nb    = cloud_sample_noise(noise, samp, uvw_b);
    float wb     = nb.r;
    
    float w     = lerp(wa, wb, 0.35);
    float thr   = 1.0 - cumulus_coverage;
    float gain  = 1.0 / max(cumulus_coverage, 0.05);
    return saturate((w - thr) * gain);
}

float cloud_density_cumulus(float3 pos, Texture3D noise, SamplerState samp, float weather)
{
    // height and shell relative math always uses the real position, only the noise lookups
    // are translated by the wind drift and the in-place evolution slide. the slide goes
    // through the noise volume's y axis so it never leaks into the 2d coverage or base-altitude
    // lookups, both of which collapse y to 0.5 internally
    float h           = length(pos - cloud_earth_center) - cloud_earth_radius;
    float3 pos_n      = pos + cloud_wind_drift(cumulus_wind_speed_mul) + cloud_evolve_offset(cumulus_evolve_rate);
    float base_offset = cloud_base_offset(noise, samp, pos_n);
    float h_norm      = (h - (cumulus_bottom_alt + base_offset)) / cumulus_thickness;
    float profile     = cloud_height_profile_cumulus(h_norm);
    if (profile <= 0.0 || weather <= 0.0) return 0.0;
    
    // single-octave domain warp at a scale ~3.5x slower than the shape tile, so whole puffs
    // get curved as units instead of being internally sheared into smoke trails. enough to
    // bend worley cell boundaries off the axis grid without dissolving the cumulus character
    float3 shape_warp = cloud_domain_warp(noise, samp, pos_n, cumulus_shape_warp_scale, cumulus_shape_warp);
    float3 uvw        = (pos_n + cumulus_wind_offset + shape_warp) * cumulus_shape_scale;
    float4 shape_n    = cloud_sample_noise(noise, samp, uvw);
    
    // base shape from low-freq perlin-worley (r), eroded by mid-frequency worley fbm (g, b)
    // channel a is the high-freq perlin used by cirrus, mixing it here gave a wispy smudge
    // instead of distinct cumulus puffs, so it stays out of the cumulus pipeline
    float fbm       = shape_n.g * 0.65 + shape_n.b * 0.35;
    float base      = cloud_remap(shape_n.r, fbm - 1.0, 1.0, 0.0, 1.0);
    base            = cloud_remap(base * profile, 1.0 - weather, 1.0, 0.0, 1.0);
    if (base <= 0.0) return 0.0;
    
    // erode by mid-frequency detail at the cloud edges, gives cauliflower micro structure
    // only the medium worley fbm channel is used, the high-frequency .b channel has features
    // below the march nyquist rate and shows up as grain rather than as detail no matter how
    // many samples we average temporally
    float3 uvw_d    = (pos_n + cumulus_wind_offset * 1.7) * cumulus_detail_scale;
    float4 detail_n = cloud_sample_noise(noise, samp, uvw_d);
    float detail    = detail_n.g;
    
    // wispy edges low in the layer, harder edges high in the layer
    // detail erosion amplitude kept moderate so the high-frequency channel does not dominate
    // the cloud surface as grain. cumulus get their puffy character from the base shape, the
    // detail just breaks up the edges so they do not read as smooth blobs
    float detail_mod = lerp(1.0 - detail, detail, saturate(h_norm * 4.0));
    float density    = cloud_remap(base, detail_mod * 0.30, 1.0, 0.0, 1.0);
    return saturate(density) * cumulus_density_mul;
}

float cloud_density_cirrus(float3 pos, Texture3D noise, SamplerState samp)
{
    float h         = length(pos - cloud_earth_center) - cloud_earth_radius;
    float h_norm    = saturate((h - cirrus_bottom_alt) / cirrus_thickness);
    float profile   = cloud_height_profile_cirrus(h_norm);
    if (profile <= 0.0) return 0.0;
    
    // cirrus drifts on its own faster wind multiplier, the streak axis is still fixed in
    // world space so the wisps slide along the wind without rotating their long direction.
    // evolution adds a much slower in-place morph so wisps gently reshape over many minutes
    // without losing their characteristic streak orientation
    float3 pos_n    = pos + cloud_wind_drift(cirrus_wind_speed_mul) + cloud_evolve_offset(cirrus_evolve_rate);
    
    // moderate stretch along a horizontal axis so cirrus reads as wispy streaks, then a large
    // domain warp on top so the streaks curve and break instead of forming hard parallel bands
    float3 warp     = cloud_domain_warp(noise, samp, pos_n, 1.0 / 40000.0, 5000.0);
    float3 streched = pos_n + cirrus_wind_offset + warp;
    float along     = dot(streched, cirrus_streak_axis);
    float3 perp     = streched - cirrus_streak_axis * along;
    float3 uvw      = (perp + cirrus_streak_axis * along * 0.45) * cirrus_noise_scale;
    
    float4 n        = cloud_sample_noise(noise, samp, uvw);
    float wisp      = n.a * 0.7 + n.b * 0.3;
    
    float density   = saturate(cloud_remap(wisp, 1.0 - cirrus_coverage, 1.0, 0.0, 1.0));
    return density * profile * cirrus_density_mul;
}

// =====================================================================
// lighting
// =====================================================================

// henyey-greenstein single phase
float cloud_hg_phase(float cos_theta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4), 1.5));
}

// dual-lobe phase, forward peak for silver lining plus a soft backscatter so dark sides aren't completely flat
float cloud_phase(float cos_theta)
{
    float forward  = cloud_hg_phase(cos_theta, 0.62);
    float backward = cloud_hg_phase(cos_theta, -0.18);
    return lerp(forward, backward, 0.4);
}

// schneider's beer-powder, edges of clouds darken when looking against the sun, interiors brighten
float cloud_beer_powder(float density)
{
    return 2.0 * exp(-density) * (1.0 - exp(-density * 2.0));
}

// transmittance lut lookup matching the skysphere uv mapping, replicated here so clouds.hlsl
// stays decoupled from skysphere.hlsl's helpers (which include this file)
float2 cloud_transmittance_uv(float height, float cos_zenith)
{
    float h           = sqrt(max((height - cloud_earth_radius) / (1e5), 0.0));
    float rho         = sqrt(max(height * height - cloud_earth_radius * cloud_earth_radius, 0.0));
    float cos_horizon = -rho / height;
    
    float x_mu;
    if (cos_zenith > cos_horizon)
    {
        x_mu = 0.5 + 0.5 * (cos_zenith - cos_horizon) / (1.0 - cos_horizon);
    }
    else
    {
        x_mu = 0.5 * (cos_zenith + 1.0) / (cos_horizon + 1.0);
    }
    return float2(saturate(x_mu), h);
}

float3 cloud_sun_illuminance(float3 sample_pos, float3 sun_dir, Texture2D transmittance_lut, SamplerState samp)
{
    // sun radiance comes from the centralized helper so cloud direct lighting tracks the
    // directional light's authored color and intensity, the previous hardcoded constant
    // float3(1, 0.98, 0.95) * 20.0 was the third independent sun energy path in the
    // engine and left clouds detached from the sky scatter and the direct lighting
    float3 up   = normalize(sample_pos - cloud_earth_center);
    float h     = length(sample_pos - cloud_earth_center);
    float cos_z = dot(up, sun_dir);

    float2 uv     = cloud_transmittance_uv(h, cos_z);
    float3 trans  = transmittance_lut.SampleLevel(samp, uv, 0).rgb;
    float horizon = smoothstep(-0.05, 0.10, cos_z);

    return get_sun_radiance() * trans * horizon;
}

// integrate density along the sun direction with a geometrically growing step, gives soft self-shadowing
float cloud_sun_optical_depth_cumulus(
    float3 pos, float3 sun_dir,
    Texture3D noise, SamplerState samp,
    float weather)
{
    float optical_depth = 0.0;
    float step_len      = cloud_sun_step_base;
    float3 p            = pos;
    
    [unroll]
    for (int i = 0; i < cumulus_sun_steps; i++)
    {
        p             += sun_dir * step_len;
        float d        = cloud_density_cumulus(p, noise, samp, weather);
        optical_depth += d * step_len;
        step_len      *= cloud_sun_step_growth;
    }
    return optical_depth;
}

// schneider / bouthors multi-octave beer for in-scattered light, each octave widens the phase
// function (by shrinking g) and lowers the extinction, the deeper octaves model multiple
// scattering bouncing through the cloud volume which is what gives cumulus interiors their
// luminous from within look
float3 cloud_multiscatter_attenuation(float3 sun_light, float optical_depth, float cos_theta)
{
    float3 result = float3(0.0, 0.0, 0.0);
    [unroll]
    for (int i = 0; i < 3; i++)
    {
        float a       = pow(0.5, float(i)); // extinction shrink
        float b       = pow(0.85, float(i)); // contribution shrink, near 1 keeps deep octaves bright
        float g_scale = pow(0.5, float(i)); // g shrink, deeper octaves more isotropic
        float beer    = exp(-optical_depth * cumulus_sigma_t * a);
        float forward = cloud_hg_phase(cos_theta,  0.62 * g_scale);
        float back    = cloud_hg_phase(cos_theta, -0.18 * g_scale);
        float ph      = lerp(forward, back, 0.4);
        result       += sun_light * beer * ph * b;
    }
    return result;
}

// =====================================================================
// raymarch helpers
// =====================================================================

float2 cloud_ray_shell(float3 origin, float3 dir, float radius_low, float radius_high)
{
    float3 oc       = origin - cloud_earth_center;
    float oc_len_sq = dot(oc, oc);
    float b         = dot(dir, oc);
    
    // reject rays that intersect the planet surface before reaching the cloud layer
    // without this, a ray going slightly below the geometric horizon would find a
    // valid shell intersection on the far side of the earth and produce garbage cloud
    // samples that show up as vertical streaks at the horizon
    float earth_r_sq = cloud_earth_radius * cloud_earth_radius;
    if (oc_len_sq > earth_r_sq - 1.0)
    {
        float c_e = oc_len_sq - earth_r_sq;
        float d_e = b * b - c_e;
        if (d_e > 0.0)
        {
            float t_e = -b - sqrt(d_e);
            if (t_e > 0.0) return float2(-1.0, -1.0);
        }
    }
    
    // outer shell, always present for any ray going up
    float c_h = oc_len_sq - radius_high * radius_high;
    float d_h = b * b - c_h;
    if (d_h < 0.0) return float2(-1.0, -1.0);
    float sd_h = sqrt(d_h);
    float t_h0 = -b - sd_h;
    float t_h1 = -b + sd_h;
    if (t_h1 < 0.0) return float2(-1.0, -1.0);
    
    // inner shell, may be missed by purely tangential rays
    float c_l   = oc_len_sq - radius_low * radius_low;
    float d_l   = b * b - c_l;
    float oc_len = sqrt(oc_len_sq);
    
    float t_enter;
    float t_exit;
    
    if (oc_len < radius_low)
    {
        // origin sits below the layer, look up and the layer begins at the inner shell exit
        if (d_l < 0.0)
        {
            t_enter = max(0.0, t_h0);
            t_exit  = t_h1;
        }
        else
        {
            float t_l1 = -b + sqrt(d_l);
            t_enter    = t_l1;
            t_exit     = t_h1;
        }
    }
    else if (oc_len < radius_high)
    {
        // origin sits inside the layer, march starts right away
        t_enter = 0.0;
        if (d_l > 0.0)
        {
            float t_l0 = -b - sqrt(d_l);
            t_exit     = (t_l0 > 0.0) ? t_l0 : t_h1;
        }
        else
        {
            t_exit = t_h1;
        }
    }
    else
    {
        // origin sits above the layer, only relevant for high altitude flight
        if (t_h0 < 0.0) return float2(-1.0, -1.0);
        t_enter = t_h0;
        if (d_l > 0.0)
        {
            float t_l0 = -b - sqrt(d_l);
            t_exit     = (t_l0 > t_enter) ? t_l0 : t_h1;
        }
        else
        {
            t_exit = t_h1;
        }
    }
    
    if (t_exit <= t_enter) return float2(-1.0, -1.0);
    return float2(max(t_enter, 0.0), t_exit);
}

// raymarch a single cloud layer, accumulating in_scatter and modulating transmittance
// uses a fixed-step march with weather-driven empty-space skipping. the weather function is
// a 2d horizontal lookup so where it is zero there are no clouds for kilometres horizontally
// regardless of altitude, and we can safely stride past those regions cheaply. the fixed step
// inside cloud-bearing regions avoids the state-flipping artifacts of a coarse/fine state
// machine that would otherwise produce thin dark cracks at the boundaries
void cloud_march_cumulus(
    float3 cam_pos, float3 view_dir, float3 sun_dir,
    Texture3D noise_tex, Texture2D transmittance_lut,
    SamplerState samp_noise, SamplerState samp_lut,
    float jitter,
    inout float3 in_scatter, inout float transmittance)
{
    // shell is padded by the per-cloud base variation so the lowest-based and highest-topped
    // clouds are not clipped off when their offset moves them outside the nominal layer
    float2 shell = cloud_ray_shell(cam_pos,
        view_dir,
        cloud_earth_radius + cumulus_bottom_alt - cumulus_shell_padding,
        cloud_earth_radius + cumulus_top_alt    + cumulus_shell_padding);
    if (shell.y < 0.0) return;
    
    float t_max  = min(shell.y, 100000.0); // cap at 100 km, atmospheric perspective absorbs whatever is further
    if (shell.x >= t_max) return;
    
    float cos_th = dot(view_dir, sun_dir);
    
    const float step_size      = 125.0;    // fixed in-volume step, paired with cumulus_view_steps for ~10 km range
    const float empty_skip     = 1600.0;   // stride when the weather map says no clouds here
    const float density_thresh = 1e-4;     // lower than before, avoids binary state flicker at edges
    
    float t = shell.x + step_size * jitter;
    
    [loop]
    for (int i = 0; i < cumulus_view_steps; i++)
    {
        if (transmittance < 0.01) break;
        if (t >= t_max) break;
        
        float3 pos    = cam_pos + view_dir * t;
        float weather = cloud_weather(noise_tex, samp_noise, pos);
        
        // weather is a horizontal 2d lookup with cells ~26km across, so when it returns zero
        // we can safely skip a big chunk of horizontal distance without missing any cloud
        if (weather <= 0.0)
        {
            t += empty_skip;
            continue;
        }
        
        float density = cloud_density_cumulus(pos, noise_tex, samp_noise, weather);
        
        if (density > density_thresh)
        {
            float ext         = density * cumulus_sigma_t;
            float step_trans  = exp(-ext * step_size);
            
            float sun_od      = cloud_sun_optical_depth_cumulus(pos, sun_dir, noise_tex, samp_noise, weather);
            float3 sun_light  = cloud_sun_illuminance(pos, sun_dir, transmittance_lut, samp_lut);
            float3 sun_scat   = cloud_multiscatter_attenuation(sun_light, sun_od, cos_th);
            
            // moon lighting, mirrors the sun illuminance pipeline with -sun_dir, scaled to a
            // realistic moon-to-sun irradiance ratio and tinted cool. self-shadowing through the
            // cloud is skipped since the contribution is far below the visible threshold anyway
            float3 moon_dir   = -sun_dir;
            float3 moon_light = cloud_sun_illuminance(pos, moon_dir, transmittance_lut, samp_lut) * cloud_moon_tint;
            float3 moon_scat  = moon_light * cloud_phase(-cos_th);
            
            float h           = length(pos - cloud_earth_center) - cloud_earth_radius;
            float h_norm      = saturate((h - cumulus_bottom_alt) / cumulus_thickness);
            float3 ambient    = lerp(cloud_ambient_bottom, cloud_ambient_top, h_norm);
            ambient          *= max(luminance(sun_light) * cloud_ambient_factor + 0.005, 0.0);
            
            // night ambient floor, faint cool airglow that lights the underside of clouds even
            // on a moonless night so cumulus never go pitch black. mildly height modulated so
            // the cloud tops still read brighter than the bottoms
            ambient          += cloud_night_floor * lerp(0.7, 1.1, h_norm);
            
            float3 direct       = (sun_scat + moon_scat) * cloud_beer_powder(density);
            float3 luminance_in = direct + ambient;
            float3 s_int        = cloud_albedo * luminance_in * (1.0 - step_trans);
            
            // aerial perspective. distant clouds add less in_scatter, and they also occlude
            // less because the haze in front of them fills in whatever silhouette they would
            // have cut. without this, grazing horizon rays integrate kilometres of cloud and
            // produce a thick ring around the camera that does not exist in real life
            float aerial_t      = exp(-t * cloud_aerial_falloff);
            float effective_trans = lerp(1.0, step_trans, aerial_t);
            
            in_scatter    += transmittance * s_int * aerial_t;
            transmittance *= effective_trans;
        }
        
        t += step_size;
    }
}

void cloud_march_cirrus(
    float3 cam_pos, float3 view_dir, float3 sun_dir,
    Texture3D noise_tex, Texture2D transmittance_lut,
    SamplerState samp_noise, SamplerState samp_lut,
    float jitter,
    inout float3 in_scatter, inout float transmittance)
{
    float2 shell = cloud_ray_shell(cam_pos,
        view_dir,
        cloud_earth_radius + cirrus_bottom_alt,
        cloud_earth_radius + cirrus_top_alt);
    if (shell.y < 0.0) return;
    
    float t_max  = min(shell.y, 400000.0);
    float dt     = (t_max - shell.x) / cirrus_view_steps;
    float t      = shell.x + dt * jitter;
    float cos_th = dot(view_dir, sun_dir);
    float phase  = cloud_phase(cos_th);
    
    [loop]
    for (int i = 0; i < cirrus_view_steps; i++)
    {
        if (transmittance < 0.01) break;
        
        float3 pos     = cam_pos + view_dir * t;
        float density  = cloud_density_cirrus(pos, noise_tex, samp_noise);
        
        if (density > 0.001)
        {
            float ext        = density * cirrus_sigma_t;
            float step_trans = exp(-ext * dt);
            
            float3 sun_light = cloud_sun_illuminance(pos, sun_dir, transmittance_lut, samp_lut);
            
            // moon lighting plus a small fraction of the night floor so cirrus stay faintly
            // visible at night instead of disappearing into a black sky
            float3 moon_dir   = -sun_dir;
            float3 moon_light = cloud_sun_illuminance(pos, moon_dir, transmittance_lut, samp_lut) * cloud_moon_tint;
            float moon_phase  = cloud_hg_phase(-cos_th, 0.62);
            
            float3 scat       = sun_light * phase + moon_light * moon_phase + cloud_night_floor * 0.4;
            
            // aerial perspective, same model as the cumulus march. cirrus is higher up so the
            // haze along grazing rays is a bit thinner, but the effect is significant enough
            // that distant wisps would otherwise wrap around the camera as a high ring
            float aerial_t      = exp(-t * cloud_aerial_falloff);
            float effective_trans = lerp(1.0, step_trans, aerial_t);
            
            float3 s_int   = cloud_albedo * scat * (1.0 - step_trans);
            in_scatter    += transmittance * s_int * aerial_t;
            transmittance *= effective_trans;
        }
        
        t += dt;
        if (t >= t_max) break;
    }
}

// public entry point, called once per panorama pixel inside the skysphere kernel
// in_scatter is the radiance scattered toward the camera by the cloud volume
// transmittance is the fraction of the sky behind the clouds that reaches the camera
void clouds_evaluate(
    float3 cam_pos, float3 view_dir, float3 sun_dir,
    Texture3D noise_tex, Texture2D transmittance_lut,
    SamplerState samp_noise, SamplerState samp_lut,
    float jitter,
    out float3 in_scatter, out float transmittance)
{
    in_scatter    = float3(0.0, 0.0, 0.0);
    transmittance = 1.0;
    
    // soft horizon fade. without this the cloud silhouettes at the panorama horizon row alias
    // into the bottom hemisphere as vertical streaks, since adjacent azimuths sample wildly
    // different ray lengths and produce sharp brightness discontinuities. fading clouds to zero
    // a few degrees above the horizon also matches the natural appearance of distant cumulus,
    // which physically vanish into atmospheric haze before they reach the geometric horizon
    float horizon_fade = smoothstep(0.0, 0.06, view_dir.y);
    if (horizon_fade <= 0.0) return;
    
    // cumulus is closer to the camera looking up, march it first so cirrus appears behind any gaps
    cloud_march_cumulus(cam_pos, view_dir, sun_dir,
        noise_tex, transmittance_lut,
        samp_noise, samp_lut,
        jitter, in_scatter, transmittance);
    
    // residual transmittance carries through to the cirrus layer
    cloud_march_cirrus(cam_pos, view_dir, sun_dir,
        noise_tex, transmittance_lut,
        samp_noise, samp_lut,
        jitter, in_scatter, transmittance);
    
    // apply the horizon fade. in_scatter goes to zero with the fade, transmittance returns to 1
    // so the underlying sky is fully visible at the horizon line
    in_scatter   *= horizon_fade;
    transmittance = lerp(1.0, transmittance, horizon_fade);
}

// =====================================================================
// compute kernel - bake the 3d noise volume once at startup
// =====================================================================

#if defined(CLOUD_NOISE)
[numthreads(8, 8, 8)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint3 dims;
    tex3d_uav.GetDimensions(dims.x, dims.y, dims.z);
    if (any(tid >= dims)) return;
    
    float3 uvw = (float3(tid) + 0.5) / float3(dims);
    
    // r, low-freq perlin-worley, drives the base cumulus blob shape
    float perlin_low  = cloud_perlin_3d(uvw, 4);
    float worley_low  = 1.0 - cloud_worley_fbm(uvw, 4);
    float r           = cloud_remap(perlin_low, worley_low - 1.0, 1.0, 0.0, 1.0);
    
    // g, mid-frequency worley fbm, adds rounded sub-blobs
    float g           = cloud_worley_fbm(uvw, 6);
    
    // b, high-frequency worley fbm, fine cauliflower detail and edge erosion
    float b           = cloud_worley_fbm(uvw, 12);
    
    // a, high-frequency perlin fbm, drives the wispy cirrus streaks
    float a           = cloud_perlin_fbm(uvw, 8);
    
    tex3d_uav[tid] = float4(saturate(r), saturate(g), saturate(b), saturate(a));
}
#endif

#endif // SPARTAN_CLOUDS
