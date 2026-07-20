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
#include "planet.hlsl"
//=======================

// =====================================================================
// nubis-style volumetric clouds
// reference: andrew schneider, the real-time volumetric cloudscapes of horizon zero dawn
// =====================================================================

static const float cloud_earth_radius  = planet_earth_radius;
static const float3 cloud_earth_center = planet_earth_center;

// cumulus layer, the top is high enough for towering congestus, the per-cloud type profile
// keeps flat stratocumulus topping out in the lower quarter of the layer so the tall ceiling
// only gets filled where the weather map says a cloud is a tower
static const float cumulus_bottom_alt     = 500.0;
static const float cumulus_top_alt        = 9000.0;
static const float cumulus_thickness      = cumulus_top_alt - cumulus_bottom_alt;
static const float cumulus_shape_scale    = 1.0 / 6000.0;   // one tile of the noise volume covers 6 km horizontally, big heroic forms
static const float cumulus_coverage_scale = 1.0 / 26000.0;  // weather map domain, pushes tile repeats out toward the horizon
static const float cumulus_density_mul    = 1.15;
static const float cumulus_top_alt_max    = 8900.0;

// coverage is authored on the directional light component, 0 = clear sky, 1 = overcast,
// fair weather sits around 0.30 - 0.45, cirrus rides along at a fixed ratio so both layers
// clear out together, at the 0.38 default the ratio lands on the classic 0.22 cirrus value
float cloud_coverage_cumulus()
{
    return buffer_frame.cloud_coverage;
}

float cloud_coverage_cirrus()
{
    return buffer_frame.cloud_coverage * 0.58;
}
static const float cumulus_sigma_t        = 0.012;

// domain warp, breaks up the visible grid alignment of the noise volume so cloud puffs
// do not appear in lanes and neighbouring puffs do not share shapes. the shape warp varies
// on a scale ~3.5x slower than the shape tile, so whole puffs get translated organically
// without any internal shearing that would stretch them into strips
static const float cumulus_warp_scale         = 1.0 / 45000.0;
static const float cumulus_warp_amplitude     = 7000.0;
static const float cumulus_shape_warp_scale   = 1.0 / 22000.0;
static const float cumulus_shape_warp         = 1700.0;

// per-cloud altitude offset, breaks up the visual sense of a uniform invisible dome that
// all clouds hug. faster noise scale than the weather domain so neighbouring clouds within
// a single weather region still get different base altitudes, otherwise each cluster reads
// as a flat plate even when the cluster-to-cluster variation is large
static const float cumulus_shell_padding      = 1200.0;

// wind shear in meters at the top of the layer, higher samples fetch their noise from upwind
// so clouds visibly lean downwind with altitude like real convective towers
static const float cumulus_shear              = 350.0;

// cirrus layer
static const float cirrus_bottom_alt      = 7000.0;
static const float cirrus_top_alt         = 13000.0;
static const float cirrus_thickness       = cirrus_top_alt - cirrus_bottom_alt;
static const float cirrus_noise_scale     = 1.0 / 12000.0;
static const float cirrus_density_mul     = 0.05;
static const float cirrus_sigma_t         = 0.002;
static const float3 cirrus_streak_axis    = float3(1.0, 0.0, 0.0); // direction of the wispy streaks

float cloud_cirrus_weight(float altitude)
{
    return smoothstep(6500.0, 9500.0, altitude);
}

// constant wind offsets, drift the noise sampling so two skybox bakes show different cloudscapes
static const float3 cumulus_wind_offset   = float3(1234.0, 0.0,  567.0);
static const float3 cirrus_wind_offset    = float3(4321.0, 0.0, 8765.0);

// runtime wind drift multipliers, the cloud field is translated by -wind*time*mul over the
// horizontal noise lookups, so when the world wind vector is non zero clouds appear to flow
// along it. cumulus moves at the surface wind speed scaled by the cumulus multiplier, cirrus
// drifts faster than cumulus to match the high-altitude jet stream typically being several
// times stronger than ground level wind. only the noise sampling is translated, the sample
// altitude relative to the planet is unchanged so the cloud layer stays parked at its base.
// the scenes ship with ~8 m/s ground wind, so the effective drift lands at ~12 m/s for
// cumulus and ~40 m/s for cirrus, matching real winds at those altitudes, anything much
// faster also makes the 16 frame panorama refresh visible as stepping on cloud edges
static const float cumulus_wind_speed_mul = 1.5;
static const float cirrus_wind_speed_mul  = 5.0;

// slow evolution moves only low frequency warp fields so fine detail remains coherent
static const float cumulus_evolve_rate    = 3.0;
static const float cirrus_evolve_rate     = 1.0;

// lighting, high contrast between shaded bases and lit tops is what makes cumulus read as
// sculpted volumes instead of uniformly lit fog, the bounce keeps midday undersides grey
// blue rather than black and fades with height so tops are shaped purely by the sun
static const float3 cloud_ambient_bottom  = float3(0.10, 0.14, 0.24); // deep cool shaded base
static const float3 cloud_ambient_top     = float3(0.95, 0.95, 0.99); // bright lit top
static const float  cloud_ambient_factor  = 0.12;   // ambient strength relative to sun luminance
static const float3 cloud_ground_bounce   = float3(0.045, 0.038, 0.028); // warm terrain bounce onto the undersides
static const float  cloud_albedo          = 0.97;   // single scattering albedo, near 1 for water clouds

// night lighting, moon and airglow share night_sky_* / night_moon_to_sun from common.hlsl
static const float3 cloud_moon_tint = float3(0.92, 0.97, 1.08) * night_moon_to_sun;

// aerial perspective, atmospheric extinction along the camera-to-sample ray. a 25 km mean
// free path matches clear-day visibility, distant clouds haze out gradually toward the
// horizon instead of vanishing a few kilometers out, which previously emptied the mid sky
static const float  cloud_aerial_falloff  = 4.0e-5;

// distance adaptive steps preserve nearby detail while bounding ray cost
static const int    cumulus_march_steps   = 512;
static const float  cumulus_step_min      = 48.0;
static const float  cumulus_step_max      = 192.0;
static const float  cumulus_range_max     = 60000.0;  // haze leaves ~9 percent contribution at this distance
static const float  cirrus_range_max      = 80000.0;  // haze leaves ~4 percent here, the old 120 km tail sampled 5 km apart and striped the horizon
static const int    cirrus_view_steps     = 48;

static const int    cumulus_weather_interval = 3;

// cloud shadow map, cumulus transmittance toward the sun stored as a 2d map indexed by where
// the sun ray crosses the cloud base plane, baked once per frame by the CLOUD_SHADOW kernel
// and sampled by the volumetric fog march so the sun shafts thread through the cloud gaps
// 8 km half extent over a 1024 map gives ~16 m texels, anything visible on the ground sits
// well inside that range and the border fade hands the rest of the world back to unshadowed
static const float cloud_shadow_half_extent = 8000.0;
static const int   cloud_shadow_steps       = 32;
static const int   cloud_shadow_cirrus_steps = 8;
static const float cloud_shadow_min_sun_y   = 0.087; // ~5 degrees, keeps the projection bounded at grazing sun

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

// soft remap, same range stretch as cloud_remap but the ends are quintic faded so a density
// that sits near a threshold does not flip on and off across neighbouring noise texel planes
float cloud_remap_soft(float v, float old_min, float old_max, float new_min, float new_max)
{
    float t = saturate((v - old_min) / max(old_max - old_min, 1e-6));
    t = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    return new_min + t * (new_max - new_min);
}

// =====================================================================
// noise sampling at run time
// =====================================================================

float cloud_time()
{
    return (float)buffer_frame.time;
}

// horizontal drift applied to the cloud noise sampling positions. negative wind*time so a
// positive wind vector translates the cloud field in the +wind direction over time. the
// returned offset is meant to be added to the position before any noise lookup, never to a
// position used for height or shell intersection
float3 cloud_wind_drift(float speed_mul)
{
    float3 wind = buffer_frame.wind * speed_mul;
    return -wind * cloud_time();
}

// pure vertical drift through the cloud noise volume, used to morph cloud shapes in place
// independently of horizontal wind translation. the y axis is chosen so the offset never
// leaks into the 2d coverage lookups, which collapse their y to 0.5 internally, this keeps
// cloud regions stable on the sky while individual puffs billow and reshape inside them
float3 cloud_evolve_offset(float rate)
{
    return float3(0.0, rate * cloud_time(), 0.0);
}

static const float cloud_noise_dims = 128.0;

float4 cloud_sample_noise(Texture3D noise, SamplerState samp, float3 uvw)
{
    float3 t = uvw * cloud_noise_dims - 0.5;
    float3 i = floor(t);
    float3 f = t - i;
    f        = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float3 s = (i + 0.5 + f) / cloud_noise_dims;
    return noise.SampleLevel(samp, frac(s), 0);
}

float4 cloud_sample_noise_vertical_cubic(Texture3D noise, SamplerState samp, float3 uvw)
{
    float y = uvw.y * cloud_noise_dims - 0.5;
    float y_base = floor(y);
    float f = y - y_base;
    float f2 = f * f;
    float f3 = f2 * f;
    float w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
    float w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
    float w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
    float w3 = f3 / 6.0;
    float g0 = w0 + w1;
    float g1 = w2 + w3;
    float y0 = (y_base - 0.5 + w1 / g0) / cloud_noise_dims;
    float y1 = (y_base + 1.5 + w3 / g1) / cloud_noise_dims;
    float2 xz = uvw.xz * cloud_noise_dims - 0.5;
    float2 xz_base = floor(xz);
    float2 xz_fraction = xz - xz_base;
    xz_fraction = xz_fraction * xz_fraction * xz_fraction * (xz_fraction * (xz_fraction * 6.0 - 15.0) + 10.0);
    float2 xz_smooth = (xz_base + 0.5 + xz_fraction) / cloud_noise_dims;
    float3 uvw0 = float3(xz_smooth.x, y0, xz_smooth.y);
    float3 uvw1 = float3(xz_smooth.x, y1, xz_smooth.y);
    return noise.SampleLevel(samp, frac(uvw0), 0) * g0 + noise.SampleLevel(samp, frac(uvw1), 0) * g1;
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

float2 cloud_altitude_range(Texture3D noise, SamplerState samp, float3 pos, float cloud_type)
{
    float3 uvw = pos * (1.0 / 40000.0) + 0.347;
    uvw.y = 0.5;
    float4 altitude_noise = cloud_sample_noise(noise, samp, uvw);
    float base_noise = saturate((altitude_noise.g - 0.15) / 0.65);
    float class_noise = saturate((altitude_noise.a - 0.30) / 0.40);
    float development_noise = saturate((altitude_noise.b - 0.15) / 0.65);
    float tower_weight = smoothstep(0.55, 0.85, cloud_type);
    float mid_weight = smoothstep(0.64, 0.80, class_noise) * (1.0 - tower_weight);
    float low_base = lerp(700.0, 2000.0, base_noise);
    float mid_base = lerp(2400.0, 4300.0, base_noise);
    float tower_base = lerp(700.0, 1600.0, base_noise);
    float low_thickness = lerp(900.0, 2500.0, development_noise);
    float mid_thickness = lerp(1600.0, 3500.0, development_noise);
    float tower_thickness = lerp(4500.0, 7800.0, saturate(cloud_type * 0.65 + development_noise * 0.35));
    float base_altitude = lerp(lerp(low_base, mid_base, mid_weight), tower_base, tower_weight);
    float thickness = lerp(lerp(low_thickness, mid_thickness, mid_weight), tower_thickness, tower_weight);
    return float2(base_altitude, min(base_altitude + thickness, cumulus_top_alt_max));
}

// =====================================================================
// shape / density
// =====================================================================

// cumulus vertical profile, flat-ish bottom and rounded top, mirrors the schneider cumulus curve
// bottom stays near the nominal base so forms read as puffy volumes instead of hanging smoke
// tendrils, the top fades smoothly over the upper half so the silhouette has curving domes
float cloud_height_profile_cumulus(float h_norm)
{
    float bottom = smoothstep(-0.04, 0.22, h_norm);
    float top    = smoothstep(1.0, 0.56, h_norm);
    return saturate(bottom * top);
}

// cirrus vertical profile, soft hump centered in the layer
float cloud_height_profile_cirrus(float h_norm)
{
    return sin(saturate(h_norm) * PI);
}

// 2d procedural weather term, low-frequency horizontal coverage, drives where cumulus exists
// the light-authored coverage directly controls the fraction of horizontal area that has cloud, the
// (1 - coverage) threshold carves large clear regions instead of just biasing toward more cloud
// the position is domain-warped before sampling so the underlying noise tile grid does not
// appear as lanes of identical clumps across the sky, and a second much larger octave is
// blended in so clouds cluster into organic regions instead of an even sprinkle
// returns coverage in x and cloud type in y, type 0 is a low flat stratocumulus sheet and
// type 1 is a towering congestus filling the whole layer
float2 cloud_weather(Texture3D noise, SamplerState samp, float3 pos)
{
    // drift the coverage map along the world wind direction so cloud regions translate over
    // time instead of being parked over the same horizontal patches forever
    float3 pos_d = pos + cloud_wind_drift(cumulus_wind_speed_mul);
    pos_d.y      = 0.0;
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
    
    float w        = lerp(wa, wb, 0.35);
    float cov      = cloud_coverage_cumulus();
    float thr      = 1.0 - cov;
    float gain     = 1.0 / max(cov, 0.05);
    float coverage = saturate((w - thr) * gain);
    
    // type rides a decorrelated channel of the large-scale sample and is pulled up by the
    // coverage, so cloudy cores build into towers while fringe clouds stay low and flat.
    // worley fbm concentrates around 0.35 - 0.6, the stretch expands that band to the full
    // 0 - 1 type range, without it every cloud lands mid range and the sky shows one species
    float type = saturate((nb.g - 0.32) * 2.4 + (coverage - 0.5) * 0.8);
    return float2(coverage, type);
}

float3 cloud_rotate_detail(float3 p)
{
    return float3(dot(p, float3(0.36, 0.48, 0.80)), dot(p, float3(-0.80, 0.60, 0.0)), dot(p, float3(-0.48, -0.64, 0.60)));
}

float cloud_density_cumulus(float3 pos, Texture3D noise, SamplerState samp, float2 weather, out float h_norm)
{
    // height uses world space while wind translates only the noise field
    float h           = length(pos - cloud_earth_center) - cloud_earth_radius;
    float3 pos_n      = pos + cloud_wind_drift(cumulus_wind_speed_mul);
    float2 altitude_range = cloud_altitude_range(noise, samp, pos_n, weather.y);
    h_norm            = (h - altitude_range.x) / max(altitude_range.y - altitude_range.x, 1.0);
    float profile     = cloud_height_profile_cumulus(h_norm);
    if (profile <= 0.0 || weather.x <= 0.0)
    {
        return 0.0;
    }
    
    // wind shear, higher samples fetch their noise from upwind so the cloud leans downwind
    // with altitude, when the world wind is zero a fixed axis keeps the lean for character
    float3 wind_h    = float3(buffer_frame.wind.x, 0.0, buffer_frame.wind.z);
    float  wind_len  = length(wind_h);
    float3 shear_dir = wind_len > 1e-3 ? wind_h / wind_len : float3(1.0, 0.0, 0.0);
    pos_n           -= shear_dir * (cumulus_shear * saturate(h_norm));
    
    // single-octave domain warp at a scale ~3.5x slower than the shape tile, so whole puffs
    // get curved as units instead of being internally sheared into smoke trails. enough to
    // bend worley cell boundaries off the axis grid without dissolving the cumulus character
    float3 shape_warp = cloud_domain_warp(noise, samp, pos_n + cloud_evolve_offset(cumulus_evolve_rate), cumulus_shape_warp_scale, cumulus_shape_warp);
    float3 uvw        = (pos_n + cumulus_wind_offset + shape_warp) * cumulus_shape_scale;
    
    // mild vertical anisotropy, sheets stay a bit flatter than towers without collapsing into
    // thin smoke pancakes
    uvw.y            *= lerp(1.12, 1.0, weather.y);
    float4 shape_n    = cloud_sample_noise_vertical_cubic(noise, samp, uvw);
    
    // base shape from low-freq perlin-worley (r), lightly eroded by mid-frequency worley fbm
    // softer carve keeps rounded cauliflower cells instead of pointy smoke filaments
    float fbm       = shape_n.g * 0.85 + shape_n.b * 0.15;
    float base      = cloud_remap_soft(shape_n.r, fbm - 0.82, 1.0, 0.0, 1.0);
    base            = cloud_remap_soft(base * profile, 1.0 - weather.x, 1.0, 0.0, 1.0);
    if (base <= 0.0)
    {
        return 0.0;
    }
    
    float3 detail_position = cloud_rotate_detail(pos_n + shape_warp * 0.35);
    float4 detail_a = cloud_sample_noise(noise, samp, detail_position * (1.0 / 1400.0) + 0.217);
    float detail     = detail_a.g * 0.70 + detail_a.b * 0.30;
    float detail_amt = lerp(0.04, 0.08, weather.y);
    float density    = saturate(cloud_remap_soft(base, detail * detail_amt, 1.0, 0.0, 1.0));
    
    // soft silhouette, keeps fringe volume so edges read as fluffy instead of razor wisps
    density = cloud_remap_soft(density, 0.02, 0.78, 0.0, 1.0);
    return density * cumulus_density_mul;
}

// low frequency density for the sun shadow march, the horizon zero dawn trick, shadow rays
// only need the coarse silhouette so the shape warp and the detail erosion are skipped which
// halves the texture fetches of the inner loop, the slight density overestimate from the
// missing erosion only deepens self-shadow marginally and is visually near lossless
float cloud_density_cumulus_cheap(float3 pos, Texture3D noise, SamplerState samp, float2 weather)
{
    float h           = length(pos - cloud_earth_center) - cloud_earth_radius;
    float3 pos_n      = pos + cloud_wind_drift(cumulus_wind_speed_mul);
    float2 altitude_range = cloud_altitude_range(noise, samp, pos_n, weather.y);
    float h_norm      = (h - altitude_range.x) / max(altitude_range.y - altitude_range.x, 1.0);
    float profile     = cloud_height_profile_cumulus(h_norm);
    if (profile <= 0.0 || weather.x <= 0.0)
    {
        return 0.0;
    }
    
    // shear kept so the shadow column leans with the cloud body
    float3 wind_h    = float3(buffer_frame.wind.x, 0.0, buffer_frame.wind.z);
    float  wind_len  = length(wind_h);
    float3 shear_dir = wind_len > 1e-3 ? wind_h / wind_len : float3(1.0, 0.0, 0.0);
    pos_n           -= shear_dir * (cumulus_shear * saturate(h_norm));
    
    float3 uvw = (pos_n + cumulus_wind_offset) * cumulus_shape_scale;
    uvw.y     *= lerp(1.12, 1.0, weather.y);
    float4 shape_n = cloud_sample_noise_vertical_cubic(noise, samp, uvw);
    
    float fbm  = shape_n.g * 0.85 + shape_n.b * 0.15;
    float base = cloud_remap_soft(shape_n.r, fbm - 0.82, 1.0, 0.0, 1.0);
    base       = cloud_remap_soft(base * profile, 1.0 - weather.x, 1.0, 0.0, 1.0);
    return saturate(base) * cumulus_density_mul;
}

float cloud_density_cirrus(float3 pos, Texture3D noise, SamplerState samp)
{
    float h         = length(pos - cloud_earth_center) - cloud_earth_radius;
    float h_norm    = saturate((h - cirrus_bottom_alt) / cirrus_thickness);
    float profile   = cloud_height_profile_cirrus(h_norm);
    if (profile <= 0.0)
    {
        return 0.0;
    }
    
    // cirrus drifts on its own faster wind multiplier, the streak axis is still fixed in
    // world space so the wisps slide along the wind without rotating their long direction.
    // evolution adds a much slower in-place morph so wisps gently reshape over many minutes
    // without losing their characteristic streak orientation
    float3 pos_n    = pos + cloud_wind_drift(cirrus_wind_speed_mul);
    
    // moderate stretch along a horizontal axis so cirrus reads as wispy streaks, then a large
    // domain warp on top so the streaks curve and break instead of forming hard parallel bands
    float3 warp     = cloud_domain_warp(noise, samp, pos_n + cloud_evolve_offset(cirrus_evolve_rate), 1.0 / 40000.0, 5000.0);
    float3 streched = pos_n + cirrus_wind_offset + warp;
    float along     = dot(streched, cirrus_streak_axis);
    float3 perp     = streched - cirrus_streak_axis * along;
    float3 uvw      = (perp + cirrus_streak_axis * along * 0.45) * cirrus_noise_scale;
    
    float4 n        = cloud_sample_noise(noise, samp, uvw);
    float wisp      = n.a * 0.7 + n.b * 0.3;
    
    float density   = saturate(cloud_remap_soft(wisp, 1.0 - cloud_coverage_cirrus(), 1.0, 0.0, 1.0));
    return density * profile * cirrus_density_mul;
}

float cloud_density_cirrus_cheap(float3 pos, Texture3D noise, SamplerState samp)
{
    float h       = length(pos - cloud_earth_center) - cloud_earth_radius;
    float h_norm  = saturate((h - cirrus_bottom_alt) / cirrus_thickness);
    float profile = cloud_height_profile_cirrus(h_norm);
    if (profile <= 0.0)
    {
        return 0.0;
    }

    float3 pos_n = pos + cloud_wind_drift(cirrus_wind_speed_mul);
    float along  = dot(pos_n + cirrus_wind_offset, cirrus_streak_axis);
    float3 perp  = pos_n + cirrus_wind_offset - cirrus_streak_axis * along;
    float3 uvw   = (perp + cirrus_streak_axis * along * 0.45) * cirrus_noise_scale;
    float wisp   = cloud_sample_noise(noise, samp, uvw).a;
    return saturate(cloud_remap_soft(wisp, 1.0 - cloud_coverage_cirrus(), 1.0, 0.0, 1.0)) * profile * cirrus_density_mul;
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

// three-lobe phase, the tight halo lobe concentrates light into an intense silver lining on
// cloud edges within a few degrees of the sun, the broad forward lobe carries the general
// translucency and the soft backscatter keeps the dark side from going flat, the weights sum
// to one so overall energy matches a single normalized phase, g_scale lets the multiscatter
// octaves widen all three lobes together as light diffuses deeper into the volume
float cloud_phase(float cos_theta, float g_scale = 1.0)
{
    float halo    = cloud_hg_phase(cos_theta,  0.92 * g_scale);
    float forward = cloud_hg_phase(cos_theta,  0.55 * g_scale);
    float back    = cloud_hg_phase(cos_theta, -0.18 * g_scale);
    return forward * 0.50 + back * 0.35 + halo * 0.15;
}

// powder term derived from the sun-ray optical depth, thin crevices darken when the sun is
// behind the viewer while thick interiors stay lit, toward the sun it converges to 1 so the
// silver lining keeps its full forward-scatter brightness
float cloud_powder(float sun_od, float cos_theta)
{
    float powder = 1.0 - exp(-2.0 * sun_od * cumulus_sigma_t);
    return lerp(1.0, powder, saturate(0.5 - 0.5 * cos_theta));
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
    // toa sun radiance tinted by the transmittance below, clouds stay locked to the atmosphere
    float3 up   = normalize(sample_pos - cloud_earth_center);
    float h     = length(sample_pos - cloud_earth_center);
    float cos_z = dot(up, sun_dir);

    // the lut stores zero for ground occluded rays, no extra horizon fade needed
    float2 uv    = cloud_transmittance_uv(h, cos_z);
    float3 trans = transmittance_lut.SampleLevel(samp, uv, 0).rgb;

    return get_sun_radiance_toa() * trans;
}

float cloud_sun_optical_depth_cumulus(
    float3 pos, float3 sun_dir,
    Texture3D noise, SamplerState samp,
    float2 weather)
{
    float optical_depth = 0.0;
    optical_depth += cloud_density_cumulus_cheap(pos + sun_dir * 100.0, noise, samp, weather) * 200.0;
    optical_depth += cloud_density_cumulus_cheap(pos + sun_dir * 400.0, noise, samp, weather) * 500.0;
    optical_depth += cloud_density_cumulus_cheap(pos + sun_dir * 1000.0, noise, samp, weather) * 1200.0;
    optical_depth += cloud_density_cumulus_cheap(pos + sun_dir * 2600.0, noise, samp, weather) * 3000.0;
    return optical_depth;
}

float cloud_sun_optical_depth_cirrus(float3 pos, float3 sun_dir, Texture3D noise, SamplerState samp)
{
    float optical_depth = 0.0;
    static const float step_len = 500.0;
    [unroll]
    for (int i = 0; i < 3; i++)
    {
        float3 p       = pos + sun_dir * (float(i) + 0.5) * step_len;
        optical_depth += cloud_density_cirrus_cheap(p, noise, samp) * step_len;
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
        float b       = pow(0.6, float(i)); // contribution shrink, lower than the extinction ratio so shadowed cores actually go dark and forms read as sculpted
        float g_scale = pow(0.5, float(i)); // g shrink, deeper octaves more isotropic
        float beer    = exp(-optical_depth * cumulus_sigma_t * a);
        result       += sun_light * beer * cloud_phase(cos_theta, g_scale) * b;
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
    float earth_distance = 1e30;
    float earth_r_sq = cloud_earth_radius * cloud_earth_radius;
    if (oc_len_sq > earth_r_sq - 1.0)
    {
        float c_e = oc_len_sq - earth_r_sq;
        float d_e = b * b - c_e;
        if (d_e > 0.0)
        {
            float t_e = -b - sqrt(d_e);
            if (t_e > 0.0)
            {
                earth_distance = t_e;
            }
        }
    }
    
    // outer shell, always present for any ray going up
    float c_h = oc_len_sq - radius_high * radius_high;
    float d_h = b * b - c_h;
    if (d_h < 0.0)
    {
        return float2(-1.0, -1.0);
    }
    float sd_h = sqrt(d_h);
    float t_h0 = -b - sd_h;
    float t_h1 = -b + sd_h;
    if (t_h1 < 0.0)
    {
        return float2(-1.0, -1.0);
    }
    
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
        if (t_h0 < 0.0)
        {
            return float2(-1.0, -1.0);
        }
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
    
    t_exit = min(t_exit, earth_distance);
    if (t_exit <= t_enter)
    {
        return float2(-1.0, -1.0);
    }
    return float2(max(t_enter, 0.0), t_exit);
}

// cloudy weather is marched continuously, only empty weather uses coarse skips
void cloud_march_cumulus(
    float3 cam_pos, float3 view_dir, float3 sun_dir,
    Texture3D noise_tex, Texture2D transmittance_lut,
    SamplerState samp_noise, SamplerState samp_lut,
    float max_distance,
    inout float3 in_scatter, inout float transmittance,
    inout float distance_weight, inout float opacity_weight)
{
    // shell is padded by the per-cloud base variation so the lowest-based and highest-topped
    // clouds are not clipped off when their offset moves them outside the nominal layer
    float2 shell = cloud_ray_shell(cam_pos,
        view_dir,
        cloud_earth_radius + cumulus_bottom_alt - cumulus_shell_padding,
        cloud_earth_radius + cumulus_top_alt    + cumulus_shell_padding);
    if (shell.y < 0.0)
    {
        return;
    }
    
    float t_max  = min(shell.y, min(cumulus_range_max, max_distance));
    if (shell.x >= t_max)
    {
        return;
    }
    
    float cos_th = dot(view_dir, sun_dir);
    
    const float empty_skip = 1600.0;
    const float density_thresh = 1e-4;
    float t = shell.x;
    
    float2 weather   = float2(0.0, 0.0);
    int weather_age  = cumulus_weather_interval;
    
    [loop]
    for (int march_i = 0; march_i < cumulus_march_steps; march_i++)
    {
        if (transmittance < 0.01)
        {
            break;
        }
        
        float step_size = lerp(
            cumulus_step_min,
            cumulus_step_max,
            smoothstep(4000.0, 30000.0, t)
        );
        float t_s = t + step_size * 0.5;
        if (t_s >= t_max)
        {
            break;
        }
        
        float3 pos = cam_pos + view_dir * t_s;
        
        if (weather_age >= cumulus_weather_interval)
        {
            weather     = cloud_weather(noise_tex, samp_noise, pos);
            weather_age = 0;
        }
        weather_age++;
        
        // weather is a horizontal 2d lookup with cells ~26km across, so when it returns zero
        // we can safely skip a big chunk of horizontal distance without missing any cloud
        if (weather.x <= 0.0)
        {
            t += empty_skip;
            weather_age = cumulus_weather_interval;
            continue;
        }
        
        float h_norm;
        float density = cloud_density_cumulus(pos, noise_tex, samp_noise, weather, h_norm);
        h_norm        = saturate(h_norm);
        
        if (density > density_thresh)
        {
            float trans_before = transmittance;
            float ext         = density * cumulus_sigma_t;
            float step_trans  = exp(-ext * step_size);
            
            float sun_od      = cloud_sun_optical_depth_cumulus(pos, sun_dir, noise_tex, samp_noise, weather);
            float3 sun_light  = cloud_sun_illuminance(pos, sun_dir, transmittance_lut, samp_lut);
            float3 sun_scat   = cloud_multiscatter_attenuation(sun_light, sun_od, cos_th);
            
            float sun_elev = dot(sun_dir, float3(0.0, 1.0, 0.0));
            float day_w    = smoothstep(-0.05, 0.18, sun_elev);
            float night_w  = 1.0 - day_w;
            
            float3 moon_dir   = -sun_dir;
            float3 moon_light = cloud_sun_illuminance(pos, moon_dir, transmittance_lut, samp_lut) * cloud_moon_tint;
            float3 moon_scat  = moon_light * cloud_phase(-cos_th);
            
            // daytime ambient, sun driven
            float3 ambient_day = lerp(cloud_ambient_bottom, cloud_ambient_top, h_norm);
            ambient_day       *= sun_light * cloud_ambient_factor + 0.005;
            ambient_day       += sun_light * cloud_ground_bounce * saturate(1.0 - h_norm * 2.0);
            
            // night ambient, sky fill locked to night_sky_radiance so clouds cannot fall below the exposed sky
            float3 ambient_night = night_sky_radiance(h_norm * 2.0 - 1.0) + night_airglow_rad;
            ambient_night       *= lerp(0.95, 1.20, h_norm);
            
            float3 ambient = lerp(ambient_night, ambient_day, day_w);
            ambient       *= lerp(0.85, 1.0, transmittance);
            
            float moon_elev_c = dot(moon_dir, float3(0.0, 1.0, 0.0));
            float moon_vis    = smoothstep(-0.05, 0.12, moon_elev_c);
            float3 direct       = (sun_scat * day_w + moon_scat * night_w * moon_vis) * cloud_powder(sun_od, cos_th);
            float3 luminance_in = direct + ambient;
            float3 s_int        = cloud_albedo * luminance_in * (1.0 - step_trans);
            
            // aerial perspective. distant clouds add less in_scatter, and they also occlude
            // less because the haze in front of them fills in whatever silhouette they would
            // have cut. without this, grazing horizon rays integrate kilometres of cloud and
            // produce a thick ring around the camera that does not exist in real life
            float aerial_t      = exp(-t_s * cloud_aerial_falloff);
            float effective_trans = lerp(1.0, step_trans, aerial_t);
            
            in_scatter    += transmittance * s_int * aerial_t;
            transmittance *= effective_trans;
            float opacity = trans_before - transmittance;
            distance_weight += opacity * t_s;
            opacity_weight  += opacity;
        }
        
        t += step_size;
        if (t >= t_max)
        {
            break;
        }
    }
}

void cloud_march_cirrus(
    float3 cam_pos, float3 view_dir, float3 sun_dir,
    Texture3D noise_tex, Texture2D transmittance_lut,
    SamplerState samp_noise, SamplerState samp_lut,
    float max_distance,
    inout float3 in_scatter, inout float transmittance,
    inout float distance_weight, inout float opacity_weight)
{
    float2 shell = cloud_ray_shell(cam_pos,
        view_dir,
        cloud_earth_radius + cirrus_bottom_alt,
        cloud_earth_radius + cirrus_top_alt);
    if (shell.y < 0.0)
    {
        return;
    }
    
    float t_max  = min(shell.y, min(cirrus_range_max, max_distance));
    if (shell.x >= t_max)
    {
        return;
    }
    float dt     = (t_max - shell.x) / cirrus_view_steps;
    float t      = shell.x + dt * 0.5;
    float cos_th = dot(view_dir, sun_dir);
    float phase  = cloud_phase(cos_th);
    
    [loop]
    for (int i = 0; i < cirrus_view_steps; i++)
    {
        if (transmittance < 0.01)
        {
            break;
        }
        
        float3 pos     = cam_pos + view_dir * t;
        float density  = cloud_density_cirrus(pos, noise_tex, samp_noise);
        
        if (density > 0.001)
        {
            float trans_before = transmittance;
            float ext        = density * cirrus_sigma_t;
            float step_trans = exp(-ext * dt);
            
            float3 sun_light = cloud_sun_illuminance(pos, sun_dir, transmittance_lut, samp_lut);
            float sun_elev_c = dot(sun_dir, float3(0.0, 1.0, 0.0));
            float day_w_c    = smoothstep(-0.05, 0.18, sun_elev_c);
            float night_w_c  = 1.0 - day_w_c;
            
            float3 moon_dir   = -sun_dir;
            float3 moon_light = cloud_sun_illuminance(pos, moon_dir, transmittance_lut, samp_lut) * cloud_moon_tint;
            float moon_phase  = cloud_hg_phase(-cos_th, 0.62);
            
            float3 night_amb = night_sky_radiance(0.35) + night_airglow_rad * 0.8;
            float moon_elev_ci = dot(moon_dir, float3(0.0, 1.0, 0.0));
            float moon_vis_c   = smoothstep(-0.05, 0.12, moon_elev_ci);
            float cirrus_shadow = 1.0;
            if (day_w_c > 0.001)
            {
                float cirrus_sun_od = cloud_sun_optical_depth_cirrus(pos, sun_dir, noise_tex, samp_noise);
                cirrus_shadow = exp(-cirrus_sun_od * cirrus_sigma_t);
            }
            float3 scat      = sun_light * phase * cirrus_shadow * day_w_c + moon_light * moon_phase * night_w_c * moon_vis_c + night_amb * night_w_c;
            
            // aerial perspective, same model as the cumulus march. cirrus is higher up so the
            // haze along grazing rays is a bit thinner, but the effect is significant enough
            // that distant wisps would otherwise wrap around the camera as a high ring
            float aerial_t      = exp(-t * cloud_aerial_falloff);
            float effective_trans = lerp(1.0, step_trans, aerial_t);
            
            float3 s_int   = cloud_albedo * scat * (1.0 - step_trans);
            in_scatter    += transmittance * s_int * aerial_t;
            transmittance *= effective_trans;
            float opacity = trans_before - transmittance;
            distance_weight += opacity * t;
            opacity_weight  += opacity;
        }
        
        t += dt;
        if (t >= t_max)
        {
            break;
        }
    }
}

// shared world space cloud evaluator
void clouds_evaluate_detailed(
    float3 cam_pos, float3 view_dir, float3 sun_dir,
    Texture3D noise_tex, Texture2D transmittance_lut,
    SamplerState samp_noise, SamplerState samp_lut,
    float max_distance,
    out float3 in_scatter, out float transmittance, out float representative_distance)
{
    in_scatter    = float3(0.0, 0.0, 0.0);
    transmittance = 1.0;
    representative_distance = 0.0;
    float distance_weight = 0.0;
    float opacity_weight  = 0.0;

    // cumulus is closer to the camera looking up, march it first so cirrus appears behind any gaps
    cloud_march_cumulus(cam_pos, view_dir, sun_dir,
        noise_tex, transmittance_lut,
        samp_noise, samp_lut,
        max_distance, in_scatter, transmittance, distance_weight, opacity_weight);
    
    // residual transmittance carries through to the cirrus layer
    cloud_march_cirrus(cam_pos, view_dir, sun_dir,
        noise_tex, transmittance_lut,
        samp_noise, samp_lut,
        max_distance, in_scatter, transmittance, distance_weight, opacity_weight);

    representative_distance = opacity_weight > 1e-5 ? distance_weight / opacity_weight : 0.0;
}

float2 cloud_depth_uv(float2 uv, float2 jitter)
{
    return uv + jitter * float2(0.5, -0.5);
}

float3 cloud_view_direction(float2 uv)
{
    float2 depth_uv = cloud_depth_uv(uv, buffer_frame.taa_jitter_current);
    float3 far_pos  = get_position(0.0001, depth_uv);
    return normalize(far_pos - get_camera_position());
}

float cloud_scene_distance(float2 uv)
{
    uint width;
    uint height;
    tex_depth.GetDimensions(width, height);
    float2 depth_uv = saturate(cloud_depth_uv(uv, buffer_frame.taa_jitter_current));
    int2 pixel      = clamp(int2(floor(depth_uv * float2(width, height) - 0.5)), int2(0, 0), int2(width - 1, height - 1));
    float distance  = 1e30;
    [unroll]
    for (int y = 0; y <= 1; y++)
    {
        [unroll]
        for (int x = 0; x <= 1; x++)
        {
            int2 tap   = clamp(pixel + int2(x, y), int2(0, 0), int2(width - 1, height - 1));
            float raw  = tex_depth.Load(int3(tap, 0)).r;
            float2 tuv = (float2(tap) + 0.5) / float2(width, height);
            if (raw > 1e-6)
            {
                distance = min(distance, length(get_position(raw, tuv) - get_camera_position()));
            }
        }
    }
    return distance;
}

float cloud_scene_distance_at(float2 uv, Texture2D depth_tex, float2 jitter, float3 camera_position)
{
    float2 depth_uv = saturate(cloud_depth_uv(uv, jitter));
    float raw       = depth_tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), depth_uv, 0).r;
    if (raw <= 1e-6)
    {
        return 1e30;
    }
    float3 world = get_position(raw, depth_uv);
    return length(world - camera_position);
}

float2 cloud_velocity(float3 view_dir, float distance, bool is_cloud)
{
    float3 current_position = get_camera_position() + view_dir * distance;
    float3 previous_position = buffer_frame.camera_position_previous + view_dir * distance;
    if (is_cloud)
    {
        previous_position = current_position;
        float cloud_altitude = length(current_position - cloud_earth_center) - cloud_earth_radius;
        float cirrus_weight = cloud_cirrus_weight(cloud_altitude);
        float wind_multiplier = lerp(cumulus_wind_speed_mul, cirrus_wind_speed_mul, cirrus_weight);
        previous_position.xz -= buffer_frame.wind.xz * wind_multiplier * buffer_frame.delta_time;
    }

    matrix current_vp = pass_is_right_eye() ? buffer_frame.view_projection_unjittered_right : buffer_frame.view_projection_unjittered;
    matrix previous_vp = pass_is_right_eye() ? buffer_frame.view_projection_previous_unjittered_right : buffer_frame.view_projection_previous_unjittered;
    float4 current_clip = mul(float4(current_position, 1.0), current_vp);
    float4 previous_clip = mul(float4(previous_position, 1.0), previous_vp);
    float2 current_ndc = current_clip.xy / max(current_clip.w, 1e-6);
    float2 previous_ndc = previous_clip.xy / max(previous_clip.w, 1e-6);
    return current_ndc - previous_ndc;
}

float3 cloud_panorama_direction(float2 uv)
{
    float phi   = uv.x * PI2 + PI;
    float theta = (0.5 - uv.y) * PI;
    float cos_t = cos(theta);
    return normalize(float3(cos(phi) * cos_t, sin(theta), sin(phi) * cos_t));
}

#if defined(CLOUD_RENDER)
[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    tex_uav.GetDimensions(width, height);
    uint2 phase = uint2(
        buffer_frame.frame & 1u,
        (buffer_frame.frame >> 1u) & 1u
    );
    uint2 pixel = tid.xy * 2u + phase;
    if (any(pixel >= uint2(width, height)))
    {
        return;
    }

    float2 uv       = (float2(pixel) + 0.5) / float2(width, height);
    float3 view_dir = cloud_view_direction(uv);
    float max_distance = cloud_scene_distance(uv);

    float3 sun_dir = normalize(-light_parameters[0].direction);

    float3 radiance;
    float transmittance;
    float representative_distance;
    clouds_evaluate_detailed(get_camera_position(), view_dir, sun_dir, tex3d, tex, GET_SAMPLER(sampler_bilinear_wrap), GET_SAMPLER(sampler_bilinear_clamp), max_distance, radiance, transmittance, representative_distance);

    tex_uav[pixel]  = float4(radiance, transmittance);
    tex_uav2[pixel] = float4(representative_distance, 0.0, 0.0, 0.0);
}
#endif

#if defined(CLOUD_TEMPORAL)
[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    tex_uav.GetDimensions(width, height);
    if (any(tid.xy >= uint2(width, height)))
    {
        return;
    }

    int2 pixel      = int2(tid.xy);
    int2 max_pixel  = int2(width - 1, height - 1);
    float2 uv       = (float2(tid.xy) + 0.5) / float2(width, height);
    float4 current  = tex.Load(int3(pixel, 0));
    float current_distance = tex2.Load(int3(pixel, 0)).r;
    uint2 phase = uint2(
        buffer_frame.frame & 1u,
        (buffer_frame.frame >> 1u) & 1u
    );
    bool current_sample_valid = all((tid.xy & 1u) == phase);

    float4 neighborhood_min = 1e30;
    float4 neighborhood_max = -1e30;
    float4 reconstruction_sum = 0.0;
    float reconstruction_weight = 0.0;
    float distance_sum = 0.0;
    float distance_weight = 0.0;
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            int2 sample_pixel = clamp(pixel + int2(x, y), int2(0, 0), max_pixel);
            float sample_distance = tex2.Load(int3(sample_pixel, 0)).r;
            if (all((uint2(sample_pixel) & 1u) == phase))
            {
                float4 sample_value = tex.Load(int3(sample_pixel, 0));
                float weight = 1.0 / (1.0 + float(x * x + y * y));
                float sample_opacity_weight = weight * saturate(1.0 - sample_value.a);
                reconstruction_sum += sample_value * weight;
                reconstruction_weight += weight;
                if (sample_distance > 0.0)
                {
                    distance_sum += sample_distance * sample_opacity_weight;
                    distance_weight += sample_opacity_weight;
                }
                neighborhood_min = min(neighborhood_min, sample_value);
                neighborhood_max = max(neighborhood_max, sample_value);
            }
        }
    }
    if (!current_sample_valid)
    {
        current = reconstruction_sum / max(reconstruction_weight, 1e-6);
        current_distance = distance_weight > 1e-6 ? distance_sum / distance_weight : 0.0;
    }

    float3 view_dir = cloud_view_direction(uv);
    float reprojection_distance = current_distance > 0.0 ? current_distance : 10000.0;
    float3 world_position = get_camera_position() + view_dir * reprojection_distance;
    float cloud_altitude = length(world_position - cloud_earth_center) - cloud_earth_radius;
    float cirrus_weight = cloud_cirrus_weight(cloud_altitude);
    float wind_multiplier = lerp(cumulus_wind_speed_mul, cirrus_wind_speed_mul, cirrus_weight);
    world_position.xz -= buffer_frame.wind.xz * wind_multiplier * buffer_frame.delta_time;
    matrix previous_vp = pass_is_right_eye() ? buffer_frame.view_projection_previous_unjittered_right : buffer_frame.view_projection_previous_unjittered;
    float4 previous_clip = mul(float4(world_position, 1.0), previous_vp);
    float2 previous_uv = ndc_to_uv(previous_clip.xy / max(previous_clip.w, 1e-6));
    float motion = saturate(length((previous_uv - uv) * float2(width, height)) * 0.125);

    bool history_valid = buffer_pass.values[0].x <= 0.5 && motion < 0.5 && previous_clip.w > 0.0 && all(previous_uv > 0.0) && all(previous_uv < 1.0);
    float4 history = current;
    float history_distance = current_distance;
    if (history_valid)
    {
        history          = tex3.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), previous_uv, 0);
        history_distance = tex4.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), previous_uv, 0).r;
        float expected_distance = length(world_position - buffer_frame.camera_position_previous);
        bool distance_valid = current_distance <= 0.0 || (history_distance > 0.0 && abs(history_distance - expected_distance) <= max(500.0, expected_distance * 0.2));

        float2 current_depth_uv = saturate(cloud_depth_uv(uv, buffer_frame.taa_jitter_current));
        float current_depth_raw = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), current_depth_uv, 0).r;
        float2 previous_depth_uv = saturate(cloud_depth_uv(previous_uv, buffer_frame.taa_jitter_previous));
        float previous_depth_raw = tex5.SampleLevel(GET_SAMPLER(sampler_point_clamp), previous_depth_uv, 0).r;
        bool depth_valid;
        if (current_depth_raw <= 1e-6)
        {
            depth_valid = previous_depth_raw <= 1e-6;
        }
        else
        {
            float3 scene_world = get_position(current_depth_raw, current_depth_uv);
            float4 scene_previous_clip = mul(float4(scene_world, 1.0), previous_vp);
            float2 scene_previous_uv = ndc_to_uv(scene_previous_clip.xy / max(scene_previous_clip.w, 1e-6));
            previous_depth_uv = saturate(cloud_depth_uv(scene_previous_uv, buffer_frame.taa_jitter_previous));
            previous_depth_raw = tex5.SampleLevel(GET_SAMPLER(sampler_point_clamp), previous_depth_uv, 0).r;
            float expected_previous_depth = linearize_depth(scene_previous_clip.z / max(scene_previous_clip.w, 1e-6));
            float sampled_previous_depth  = linearize_depth(previous_depth_raw);
            depth_valid = scene_previous_clip.w > 0.0 && all(scene_previous_uv > 0.0) && all(scene_previous_uv < 1.0) && abs(sampled_previous_depth - expected_previous_depth) <= max(2.0, expected_previous_depth * 0.02);
        }
        history_valid = distance_valid && depth_valid && !any(isnan(history));
    }

    if (history_valid)
    {
        float distance_stability = smoothstep(6000.0, 25000.0, max(current_distance, history_distance));
        float4 neighborhood_range = neighborhood_max - neighborhood_min;
        float4 clamp_margin = neighborhood_range * lerp(0.25, 1.0, distance_stability);
        history = clamp(history, neighborhood_min - clamp_margin, neighborhood_max + clamp_margin);
        float current_weight = lerp(lerp(0.06, 0.025, distance_stability), lerp(0.35, 0.12, distance_stability), motion);
        current_weight *= current_sample_valid ? 1.0 : lerp(0.35, 0.15, distance_stability);
        current = lerp(history, current, current_weight);
        current_distance = lerp(history_distance, current_distance, current_weight);
    }
    if (current.a > 0.999)
    {
        current_distance = 0.0;
    }

    tex_uav[tid.xy]  = current;
    tex_uav2[tid.xy] = float4(current_distance, 0.0, 0.0, 0.0);
}
#endif

#if defined(CLOUD_COMPOSITE)
[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    tex_uav.GetDimensions(width, height);
    if (any(tid.xy >= uint2(width, height)))
    {
        return;
    }

    uint cloud_width;
    uint cloud_height;
    tex2.GetDimensions(cloud_width, cloud_height);
    float2 uv = (float2(tid.xy) + 0.5) / float2(width, height);
    float scene_distance = cloud_scene_distance_at(uv, tex_depth, buffer_frame.taa_jitter_current, get_camera_position());
    float2 cloud_position = uv * float2(cloud_width, cloud_height) - 0.5;
    int2 cloud_base = int2(floor(cloud_position));
    float2 cloud_fraction = frac(cloud_position);
    float4 cloud_sum = 0.0;
    float weight_sum = 0.0;
    float cloud_distance_sum = 0.0;
    float cloud_distance_weight = 0.0;

    [unroll]
    for (int y = 0; y <= 1; y++)
    {
        [unroll]
        for (int x = 0; x <= 1; x++)
        {
            int2 cloud_pixel = clamp(cloud_base + int2(x, y), int2(0, 0), int2(cloud_width - 1, cloud_height - 1));
            float4 cloud_value = tex2.Load(int3(cloud_pixel, 0));
            float cloud_distance = tex3.Load(int3(cloud_pixel, 0)).r;
            float2 tap_uv = (float2(cloud_pixel) + 0.5) / float2(cloud_width, cloud_height);
            float tap_scene_distance = cloud_scene_distance_at(tap_uv, tex_depth, buffer_frame.taa_jitter_current, get_camera_position());
            float2 bilinear_axis = lerp(1.0 - cloud_fraction, cloud_fraction, float2(x, y));
            float bilinear_weight = bilinear_axis.x * bilinear_axis.y;
            float depth_weight = exp(-abs(tap_scene_distance - scene_distance) / max(10.0, min(tap_scene_distance, scene_distance) * 0.01));
            float visibility = cloud_distance <= 0.0 || cloud_distance < scene_distance ? 1.0 : 0.0;
            float weight = bilinear_weight * depth_weight * visibility;
            cloud_sum += cloud_value * weight;
            weight_sum += weight;
            if (cloud_distance > 0.0)
            {
                float opacity_weight = weight * saturate(1.0 - cloud_value.a);
                cloud_distance_sum += cloud_distance * opacity_weight;
                cloud_distance_weight += opacity_weight;
            }
        }
    }

    float4 cloud_value = weight_sum > 1e-5 ? cloud_sum / weight_sum : float4(0.0, 0.0, 0.0, 1.0);
    float4 scene_color = tex.Load(int3(tid.xy, 0));
    tex_uav[tid.xy] = float4(scene_color.rgb * cloud_value.a + cloud_value.rgb, scene_color.a);
    float resolved_cloud_distance = cloud_distance_weight > 1e-5 ? cloud_distance_sum / cloud_distance_weight : 0.0;
    bool has_cloud = resolved_cloud_distance > 0.0 && cloud_value.a < 0.98;
    bool has_sky = scene_distance > 1e20;
    float4 velocity_output = tex4.Load(int3(tid.xy, 0));
    if (has_cloud || has_sky)
    {
        float velocity_distance = has_cloud ? resolved_cloud_distance : 10000.0;
        float2 velocity = cloud_velocity(cloud_view_direction(uv), velocity_distance, has_cloud);
        velocity_output.xy = velocity;
        velocity_output.z = 0.0;
    }
    tex_uav2[tid.xy] = velocity_output;
}
#endif

#if defined(CLOUD_ENVIRONMENT)
[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    tex_uav.GetDimensions(width, height);
    if (any(tid.xy >= uint2(width, height)))
    {
        return;
    }

    float2 uv       = (float2(tid.xy) + 0.5) / float2(width, height);
    float3 view_dir = cloud_panorama_direction(uv);
    float3 clear_sky = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv, 0).rgb;

    float3 sun_dir = normalize(-light_parameters[0].direction);
    float3 radiance;
    float transmittance;
    float representative_distance;
    clouds_evaluate_detailed(get_camera_position(), view_dir, sun_dir, tex3d, tex2, GET_SAMPLER(sampler_bilinear_wrap), GET_SAMPLER(sampler_bilinear_clamp), 1e30, radiance, transmittance, representative_distance);
    tex_uav[tid.xy] = float4(clear_sky * transmittance + radiance, 1.0);
}
#endif

// =====================================================================
// cloud shadow map
// =====================================================================

// projects a world position along the sun onto the cloud base plane, the crossing point is
// the shadow map's index so every altitude below the clouds gets correct shadow parallax.
// the map is centered on the camera's own crossing so nearby samples land mid map, both the
// bake and every consumer derive the mapping from camera and sun alone, no cpu plumbing
float2 cloud_shadow_plane_uv(float3 pos, float3 sun_dir, float3 cam_pos, float texel_size)
{
    float  sun_y    = max(sun_dir.y, cloud_shadow_min_sun_y);
    float2 slope    = sun_dir.xz / sun_y;
    float2 crossing = pos.xz     + slope * (cumulus_bottom_alt - pos.y);
    float2 anchor   = cam_pos.xz + slope * (cumulus_bottom_alt - cam_pos.y);
    anchor           = floor(anchor / texel_size + 0.5) * texel_size;
    return (crossing - anchor) / (2.0 * cloud_shadow_half_extent) + 0.5;
}

// transmittance toward the sun at a world position, fades to unshadowed at the map border so
// the clamped edge texel does not smear one shadow value across the world beyond the extent.
// the map is soft blurred after bake and carries a mip chain, so a single trilinear fetch is
// enough, distance based lod kills the screen space moire that sharp texel edges produced when
// the projected map was minified on the ground
float cloud_shadow_sample(Texture2D shadow_map, SamplerState samp, float3 pos, float3 sun_dir, float3 cam_pos)
{
    uint shadow_width;
    uint shadow_height;
    shadow_map.GetDimensions(shadow_width, shadow_height);
    float texel_size = 2.0 * cloud_shadow_half_extent / float(shadow_width);
    float2 uv     = cloud_shadow_plane_uv(pos, sun_dir, cam_pos, texel_size);
    float2 border = min(uv, 1.0 - uv);
    float  inside = smoothstep(0.0, 0.04, min(border.x, border.y));
    
    float lod   = log2(max(length(pos.xz - cam_pos.xz) * 0.0015, 1.0));
    float trans = shadow_map.SampleLevel(samp, uv, lod).r;
    return lerp(1.0, trans, inside);
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
    if (any(tid >= dims))
    {
        return;
    }
    
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

// =====================================================================
// compute kernel - bake the sun-projected cloud shadow map once per frame
// =====================================================================

#if defined(CLOUD_SHADOW)
[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    float2 res;
    tex_uav.GetDimensions(res.x, res.y);
    if (any(tid.xy >= uint2(res)))
    {
        return;
    }
    
    float3 sun_dir = normalize(-light_parameters[0].direction);
    if (sun_dir.y <= 0.0)
    {
        tex_uav[tid.xy] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }
    
    // texel to base plane crossing, inverse of cloud_shadow_plane_uv with pos on the plane
    float3 cam_pos  = get_camera_position();
    float  sun_y    = max(sun_dir.y, cloud_shadow_min_sun_y);
    float2 slope    = sun_dir.xz / sun_y;
    float2 anchor   = cam_pos.xz + slope * (cumulus_bottom_alt - cam_pos.y);
    float texel_size = 2.0 * cloud_shadow_half_extent / res.x;
    anchor           = floor(anchor / texel_size + 0.5) * texel_size;
    float2 uv       = (float2(tid.xy) + 0.5) / res;
    float2 crossing = anchor + (uv - 0.5) * (2.0 * cloud_shadow_half_extent);
    
    // march the padded shell along the sun ray passing through the crossing point, the cheap
    // low frequency density is enough since the fog only needs the coarse shadow silhouette
    float3 dir   = normalize(float3(slope.x, 1.0, slope.y));
    float  t_pad = cumulus_shell_padding / dir.y;
    float3 p0    = float3(crossing.x, cumulus_bottom_alt, crossing.y) - dir * t_pad;
    float  span  = (cumulus_thickness + 2.0 * cumulus_shell_padding) / dir.y;
    float  dt    = span / float(cloud_shadow_steps);
    
    // coherent midpoint sampling, a per-texel jitter decorrelates neighbouring texels and
    // prints a dot grid onto the ground, without it any residual march error stays smooth
    float optical_depth = 0.0;
    float2 weather      = float2(0.0, 0.0);
    int weather_age     = cumulus_weather_interval * 2;
    
    [loop]
    for (int i = 0; i < cloud_shadow_steps; i++)
    {
        float3 p = p0 + dir * ((float(i) + 0.5) * dt);
        
        if (weather_age >= cumulus_weather_interval * 2)
        {
            weather     = cloud_weather(tex3d, GET_SAMPLER(sampler_bilinear_wrap), p);
            weather_age = 0;
        }
        weather_age++;
        
        if (weather.x > 0.0)
        {
            optical_depth += cloud_density_cumulus_cheap(p, tex3d, GET_SAMPLER(sampler_bilinear_wrap), weather) * dt;
        }
    }
    
    float cirrus_dt = (cirrus_thickness / dir.y) / float(cloud_shadow_cirrus_steps);
    float3 cirrus_p0 = float3(crossing.x, cumulus_bottom_alt, crossing.y) + dir * ((cirrus_bottom_alt - cumulus_bottom_alt) / dir.y);
    float cirrus_optical_depth = 0.0;
    [unroll]
    for (int i = 0; i < cloud_shadow_cirrus_steps; i++)
    {
        float3 p = cirrus_p0 + dir * ((float(i) + 0.5) * cirrus_dt);
        cirrus_optical_depth += cloud_density_cirrus_cheap(p, tex3d, GET_SAMPLER(sampler_bilinear_wrap)) * cirrus_dt;
    }

    float trans = exp(-optical_depth * cumulus_sigma_t - cirrus_optical_depth * cirrus_sigma_t);
    tex_uav[tid.xy] = float4(trans, trans, trans, 1.0);
}
#endif

#endif // SPARTAN_CLOUDS
