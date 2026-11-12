/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_CLOUDS
#define SPARTAN_CLOUDS

//= includes ============
#include "../common.hlsl"
#include "planet.hlsl"
#include "cloud_shadow.hlsl"
#include "sky_view.hlsl"
//=======================

// =====================================================================
// nubis-style volumetric clouds
// reference: andrew schneider, the real-time volumetric cloudscapes of horizon zero dawn
// =====================================================================

static const float cloud_earth_radius  = planet_earth_radius;
static const float3 cloud_earth_center = planet_earth_center;

// cumulus layer, the top is high enough for towering congestus, the per-cloud type profile
// keeps flat stratocumulus topping out in the lower quarter of the layer so the tall ceiling
// only gets filled where the weather map says a cloud is a tower, the base altitude
// cumulus_bottom_alt lives in cloud_shadow.hlsl since the shadow map is indexed on that plane
static const float cumulus_top_alt        = 8000.0;
static const float cumulus_thickness      = cumulus_top_alt - cumulus_bottom_alt;
static const float cumulus_shape_scale    = 1.0 / 12000.0;
static const float cumulus_detail_scale   = 1.0 / 2600.0;
static const float cumulus_coverage_scale = 1.0 / 60000.0;
static const float cumulus_coverage_open   = 0.38;   // coverage under which the base shape leaves no cloud
static const float cumulus_coverage_closed = 0.70;   // coverage over which the base shape leaves no gap
static const float cumulus_weather_fringe  = 0.70;   // share of the weather signal a cloud field takes from its clear edge to its closed core
static const float cumulus_density_mul    = 1.30;

// coverage is authored on the directional light, 0 is clear and 1 is overcast
float cloud_coverage_cumulus()
{
    return saturate(buffer_frame.cloud_coverage);
}

float cloud_coverage_cirrus()
{
    return saturate(buffer_frame.cloud_coverage) * 0.58;
}
static const float cumulus_sigma_t        = 0.012;

static const float cumulus_shell_padding      = 0.0;

// cirrus layer
static const float cirrus_bottom_alt      = 7000.0;
static const float cirrus_top_alt         = 13000.0;
static const float cirrus_thickness       = cirrus_top_alt - cirrus_bottom_alt;
static const float cirrus_noise_scale     = 1.0 / 12000.0;
static const float cirrus_density_mul     = 0.015;
static const float cirrus_sigma_t         = 0.001;
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
// the scenes ship with ~8 m/s ground wind, so the effective drift lands at ~12 m/s for
// cumulus and ~40 m/s for cirrus, matching real winds at those altitudes, anything much
// faster also makes the 16 frame panorama refresh visible as stepping on cloud edges
static const float cumulus_wind_speed_mul = 1.5;
static const float cirrus_wind_speed_mul  = 5.0;

static const float cirrus_evolve_rate     = 1.0;

// lighting, high contrast between shaded bases and lit tops is what makes cumulus read as
// sculpted volumes instead of uniformly lit fog. the sun is the only strong light, a sunlit
// face comes out at ~0.2 of the sun illuminance, which is what an optically thick water cloud
// reflects, while the shade is filled by the sky from above and by the ground from below, both
// an order of magnitude dimmer. a constant white ambient as strong as the sun term used to
// sit on top of it and flattened every cloud into an evenly lit grey mass
static const float  cloud_albedo           = 0.97;   // single scattering albedo, near 1 for water clouds
static const float  cloud_ambient_sky      = 0.80;   // share of the mean sky radiance a cloud top scatters back
static const float  cloud_ambient_sky_base = 0.20;   // share of it that still reaches the base through the cloud
static const float3 cloud_ground_albedo    = float3(0.20, 0.18, 0.14); // warm terrain under the clouds

// multiple scattering octaves, every octave halves the extinction and the phase eccentricity
// and keeps 0.75 of the energy, five of them carry enough light into the cloud to reach the
// albedo above, three at 0.6 left the sunlit side three times too dark
static const int    cloud_scatter_octaves       = 5;
static const float  cloud_scatter_extinction    = 0.5;
static const float  cloud_scatter_contribution  = 0.75;
static const float  cloud_scatter_eccentricity  = 0.5;

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
static const float  cumulus_step_far      = 384.0;    // step at the range limit, a cloud pixel is ~100 m wide out there
static const float  cumulus_range_max     = 60000.0;  // haze leaves ~9 percent contribution at this distance

// empty space is crossed in long probe steps, a probe that lands in a cloud marches its own
// interval again in fine steps, so nothing ahead of the probe point is lost, and after a few
// empty fine steps the ray goes back to probing
static const float  cumulus_probe_step_mul  = 3.0;
static const float  cumulus_probe_step_add  = 240.0;  // the probe is never this much longer than the fine step
static const int    cumulus_probe_resume    = 4;      // empty fine steps before probing again

// light march
static const int    cumulus_light_steps         = 6;
static const float  cumulus_light_tau_max       = 30.0;  // past this depth the sun adds a few thousandths of its illuminance, well under the ambient fill
static const float  cumulus_light_reuse_density = 0.3;   // thinner samples light every other step, their sun depth barely changes in one step
static const float  cumulus_light_jitter_max    = 600.0;  // longest stretch of a light step its sample is jittered over
static const float  cumulus_light_depth_max     = 3000.0; // deepest cloud a single light step can stand for
static const float  cirrus_range_max      = 80000.0;  // haze leaves ~4 percent here, the old 120 km tail sampled 5 km apart and striped the horizon
static const int    cirrus_view_steps     = 48;

static const int    cumulus_weather_interval = 3;

// march steps of the CLOUD_SHADOW bake, the map's projection and sampling live in cloud_shadow.hlsl
static const int   cloud_shadow_steps       = 32;

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

float cloud_remap(
    float value,
    float old_min,
    float old_max,
    float new_min,
    float new_max)
{
    float range = max(old_max - old_min, 1e-6);
    float t     = saturate((value - old_min) / range);
    return lerp(new_min, new_max, t);
}

// soft remap keeps threshold transitions smooth across neighbouring noise texels
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
// per-load seed offset rides along so every level load lands on a different cloudscape. the
// returned offset is meant to be added to the position before any noise lookup, never to a
// position used for height or shell intersection
float3 cloud_wind_drift(float speed_mul)
{
    float3 wind = float3(buffer_frame.wind.x, 0.0, buffer_frame.wind.z) * speed_mul;
    float3 seed = float3(buffer_frame.cloud_seed_offset.x, 0.0, buffer_frame.cloud_seed_offset.y);
    return seed - wind * cloud_time();
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

// =====================================================================
// shape / density
// =====================================================================

float cloud_height_profile_cumulus(
    float h_norm,
    float cloud_type)
{
    float bottom = smoothstep(0.0, 0.10, h_norm);
    float stratus = bottom *
        smoothstep(0.32, 0.20, h_norm);
    float stratocumulus = bottom *
        smoothstep(0.65, 0.35, h_norm);
    float cumulus = bottom *
        smoothstep(1.0, 0.62, h_norm);

    float low_types = lerp(
        stratus,
        stratocumulus,
        saturate(cloud_type * 2.0)
    );
    return lerp(
        low_types,
        cumulus,
        saturate(cloud_type * 2.0 - 1.0)
    );
}

// cirrus vertical profile, soft hump centered in the layer
float cloud_height_profile_cirrus(float h_norm)
{
    return sin(saturate(h_norm) * PI);
}

float2 cloud_weather(Texture3D noise, SamplerState samp, float3 pos)
{
    float3 pos_d = pos + cloud_wind_drift(cumulus_wind_speed_mul);
    pos_d.y      = 0.0;

    float3 uvw_a = pos_d * cumulus_coverage_scale;
    uvw_a.y      = 0.5;
    float4 na    = cloud_sample_noise(noise, samp, uvw_a + 0.123);

    float3 uvw_b = pos_d *
        (cumulus_coverage_scale * 0.37) +
        0.781;
    uvw_b.y      = 0.5;
    float4 nb    = cloud_sample_noise(noise, samp, uvw_b);

    // the worley channel is close to uniform between 0.33 and 0.67, two scales of it add up to a
    // signal that a smoothstep flattens, so a threshold on it cuts off a known share of the sky
    // wherever the camera is. the perlin worley channel is zero over half of the volume and 0.1 on
    // average, with it alone as the signal the default 0.8 coverage left an almost clear sky
    float cells_a = saturate((na.g - 0.33) / 0.34);
    float cells_b = saturate((nb.g - 0.33) / 0.34);
    float signal  = smoothstep(0.0, 1.0, cells_a * 0.6 + cells_b * 0.4 + na.r * 0.25);

    // local fill, the share of the sky a cloud field closes, 0 at its edge and 1 in its core. the
    // fields grow with the slider, 0 is clear and 1 leaves no gap. the curve keeps the low half of
    // the slider for fair weather, 0.5 is scattered cumulus and the default 0.8 covers half of the sky
    float authored = pow(cloud_coverage_cumulus(), 1.9);
    float edge     = lerp(1.0, -cumulus_weather_fringe, authored);
    float fill     = saturate((signal - edge) / cumulus_weather_fringe);

    // towers stand in the cores of the fields, the low flat types fill the rest of the sky
    float type_signal = smoothstep(0.0, 1.0, saturate((nb.b - 0.33) / 0.34) * 0.7 + cells_a * 0.3);
    float type        = lerp(0.2, 1.0, type_signal * type_signal);
    if (fill <= 0.0)
    {
        return float2(0.0, type);
    }

    // the base shape sits between 0.3 and 0.45 nearly everywhere, so it goes from scattered puffs
    // to a closed deck inside a narrow band of coverage, past that band a deck only gets thicker
    float coverage = lerp(cumulus_coverage_open, cumulus_coverage_closed, pow(fill, 0.35));
    coverage       = lerp(coverage, 1.0, fill * smoothstep(0.7, 1.0, authored));
    return float2(coverage, type);
}

float3 cloud_rotate_detail(float3 p)
{
    return float3(dot(p, float3(0.36, 0.48, 0.80)), dot(p, float3(-0.80, 0.60, 0.0)), dot(p, float3(-0.48, -0.64, 0.60)));
}

float cloud_shape_cumulus(float4 shape_noise)
{
    float worley_fbm =
        shape_noise.g * 0.625 +
        shape_noise.b * 0.250 +
        shape_noise.a * 0.125;
    return cloud_remap(
        shape_noise.r,
        -(1.0 - worley_fbm),
        1.0,
        0.0,
        1.0
    );
}

float cloud_density_cumulus(float3 pos, Texture3D noise, SamplerState samp, float2 weather, out float h_norm)
{
    float altitude = length(pos - cloud_earth_center) -
        cloud_earth_radius;
    h_norm = (altitude - cumulus_bottom_alt) /
        cumulus_thickness;
    float profile = cloud_height_profile_cumulus(
        h_norm,
        weather.y
    );
    if (profile <= 0.0 || weather.x <= 0.0)
    {
        return 0.0;
    }

    float3 pos_n = pos +
        cloud_wind_drift(cumulus_wind_speed_mul) +
        cumulus_wind_offset;
    float4 shape_noise = cloud_sample_noise(
        noise,
        samp,
        pos_n * cumulus_shape_scale
    );
    float base_shape = cloud_shape_cumulus(shape_noise);
    float dimensional_profile = profile * weather.x;
    float density = saturate(
        base_shape -
        (1.0 - dimensional_profile)
    );
    if (density <= 0.0)
    {
        return 0.0;
    }

    float3 detail_position = cloud_rotate_detail(pos_n);
    float4 detail_noise = cloud_sample_noise(
        noise,
        samp,
        detail_position * cumulus_detail_scale +
        0.217
    );
    float detail_fbm =
        detail_noise.g * 0.625 +
        detail_noise.b * 0.250 +
        detail_noise.a * 0.125;
    detail_fbm = lerp(
        1.0 - detail_fbm,
        detail_fbm,
        saturate(h_norm * 10.0)
    );
    density = cloud_remap(
        density,
        detail_fbm * 0.30,
        1.0,
        0.0,
        1.0
    );
    return pow(saturate(density), 0.65) *
        cumulus_density_mul;
}

// low frequency density for the sun shadow march, the horizon zero dawn trick, shadow rays
// only need the coarse silhouette so the shape warp and the detail erosion are skipped which
// halves the texture fetches of the inner loop, the slight density overestimate from the
// missing erosion only deepens self-shadow marginally and is visually near lossless
float cloud_density_cumulus_cheap(float3 pos, Texture3D noise, SamplerState samp, float2 weather)
{
    float altitude = length(pos - cloud_earth_center) -
        cloud_earth_radius;
    float h_norm = (altitude - cumulus_bottom_alt) /
        cumulus_thickness;
    float profile = cloud_height_profile_cumulus(
        h_norm,
        weather.y
    );
    if (profile <= 0.0 || weather.x <= 0.0)
    {
        return 0.0;
    }

    float3 pos_n = pos +
        cloud_wind_drift(cumulus_wind_speed_mul) +
        cumulus_wind_offset;
    float4 shape_noise = cloud_sample_noise(
        noise,
        samp,
        pos_n * cumulus_shape_scale
    );
    float base_shape = cloud_shape_cumulus(shape_noise);
    float dimensional_profile = profile * weather.x;
    float density = saturate(
        base_shape -
        (1.0 - dimensional_profile)
    );
    return pow(density, 0.65) *
        cumulus_density_mul;
}

float cloud_density_cirrus(float3 pos, Texture3D noise, SamplerState samp)
{
    float h         = length(pos - cloud_earth_center) - cloud_earth_radius;
    if (h <= cirrus_bottom_alt || h >= cirrus_top_alt) return 0.0;
    float h_norm    = (h - cirrus_bottom_alt) / cirrus_thickness;
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
    if (h <= cirrus_bottom_alt || h >= cirrus_top_alt) return 0.0;
    float h_norm  = (h - cirrus_bottom_alt) / cirrus_thickness;
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

// powder uses local density so unshadowed edges do not lose their direct light
float cloud_powder(float density, float cos_theta)
{
    float powder      = 1.0 - exp(-2.0 * density);
    float back_weight = saturate(0.5 - 0.5 * cos_theta);
    float powder_term = lerp(0.75, 1.0, powder);
    return lerp(1.0, powder_term, back_weight);
}

float3 cloud_sun_illuminance(float3 sample_pos, float3 sun_dir, Texture2D transmittance_lut, SamplerState samp)
{
    // toa sun radiance tinted by the transmittance below, clouds stay locked to the atmosphere
    float3 up   = normalize(sample_pos - cloud_earth_center);
    float h     = length(sample_pos - cloud_earth_center);
    float cos_z = dot(up, sun_dir);

    // Shared horizon mapping and planet occlusion with the sky.
    float3 trans = planet_transmittance(transmittance_lut, samp, h, cos_z);

    return get_sun_radiance_toa() * trans;
}

float2 cloud_ray_shell(float3 origin, float3 dir, float radius_low, float radius_high);

float cloud_sun_optical_depth_cumulus(
    float3 pos, float3 sun_dir,
    Texture3D noise, SamplerState samp,
    float jitter)
{
    float2 shell = cloud_ray_shell(pos, sun_dir,
        cloud_earth_radius + cumulus_bottom_alt,
        cloud_earth_radius + cumulus_top_alt);
    if (shell.y <= 0.0) return 0.0;

    // Cover the visible cloud field, including grazing light paths. Exponential
    // spacing retains local self-shadow detail without a fixed 4.9 km cutoff.
    float span = min(shell.y, cumulus_range_max);
    float optical_depth = 0.0;
    float previous_t = 0.0;
    float growth = pow(1.0 + span / 200.0, 1.0 / float(cumulus_light_steps));
    float scale = 1.0;
    [loop]
    for (int i = 0; i < cumulus_light_steps; i++)
    {
        scale *= growth;
        float t = 200.0 * (scale - 1.0);
        float offset = frac(jitter + float(i) * 0.6180339887);
        // a low sun stretches the last steps over tens of kilometers. one sample jittered over all of
        // that either hits a cloud or misses it, pixel by pixel, which showed as dark grain at sunset,
        // so a long step jitters around its middle only and stays coherent across the screen
        float dt          = t - previous_t;
        float jitter_span = min(dt, cumulus_light_jitter_max);
        float3 p = pos + sun_dir * (previous_t + 0.5 * (dt - jitter_span) + jitter_span * offset);
        // Weather varies along the light path, especially near sunset.
        float2 weather = cloud_weather(noise, samp, p);
        // no cloud is as deep as a long step, a wisp caught by one must not shade like a wall
        optical_depth += cloud_density_cumulus_cheap(p, noise, samp, weather) * min(dt, cumulus_light_depth_max);
        previous_t = t;
        // the sample is already in deep shade, the rest of the path cannot change what is seen
        if (optical_depth * cumulus_sigma_t > cumulus_light_tau_max)
        {
            break;
        }
    }
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
// luminous from within look. the phase of every octave depends on the view and light
// directions alone, so it is evaluated once per ray with the octave's energy folded in
void cloud_scatter_phases(float cos_theta, out float phases[cloud_scatter_octaves])
{
    float contribution = 1.0;
    float g_scale      = 1.0;
    [unroll]
    for (int i = 0; i < cloud_scatter_octaves; i++)
    {
        phases[i]     = cloud_phase(cos_theta, g_scale) * contribution;
        contribution *= cloud_scatter_contribution;
        g_scale      *= cloud_scatter_eccentricity;
    }
}

// share of the light's illuminance scattered toward the viewer behind the given optical depth
float cloud_scatter_attenuation(float optical_depth, float phases[cloud_scatter_octaves])
{
    float tau    = optical_depth * cumulus_sigma_t;
    float result = 0.0;
    [unroll]
    for (int i = 0; i < cloud_scatter_octaves; i++)
    {
        result += exp(-tau) * phases[i];
        tau    *= cloud_scatter_extinction;
    }
    return result;
}

// top of a cloud of the given type as a fraction of the layer, the middle of the fade out of
// its height profile
float cloud_top_norm(float cloud_type)
{
    return lerp(lerp(0.26, 0.50, saturate(cloud_type * 2.0)), 0.81, saturate(cloud_type * 2.0 - 1.0));
}

// mean radiance of the sky dome from the sun half of the sky view lut, the zenith and a ring
// at 30 degrees of elevation, this is the light that fills the shaded side of a cloud, it
// follows the sky through sunset where the shade turns blue against the orange sunlit side
float3 cloud_sky_ambient(Texture2D sky_view_lut, SamplerState samp)
{
    static const float ring_v = 0.57735; // sqrt(30 / 90), the elevation warp of the lut
    float3 sky = sample_sky_view_lut_unit(sky_view_lut, samp, float2(0.5, 1.0), 0.0);
    sky       += sample_sky_view_lut_unit(sky_view_lut, samp, float2(0.17, ring_v), 0.0);
    sky       += sample_sky_view_lut_unit(sky_view_lut, samp, float2(0.50, ring_v), 0.0);
    sky       += sample_sky_view_lut_unit(sky_view_lut, samp, float2(0.83, ring_v), 0.0);
    return sky * 0.25;
}

// =====================================================================
// raymarch helpers
// =====================================================================

float2 cloud_ray_sphere(float3 origin, float3 dir, float radius)
{
    float3 oc = origin - cloud_earth_center;
    float r = length(oc);
    float b = dot(oc, dir);
    float c = (r - radius) * (r + radius);
    float discriminant = b * b - c;
    if (discriminant < 0.0) return -1.0;
    float d = sqrt(discriminant);
    float q = -b - (b >= 0.0 ? d : -d);
    if (abs(q) < 1e-6) return 0.0;
    float other = c / q;
    return float2(min(q, other), max(q, other));
}

// Nearest contiguous interval in the cloud shell, clipped against the planet.
float2 cloud_ray_shell(float3 origin, float3 dir, float radius_low, float radius_high)
{
    float r = length(origin - cloud_earth_center);
    float2 outer = cloud_ray_sphere(origin, dir, radius_high);
    if (outer.y <= 0.0) return -1.0;
    float2 inner = cloud_ray_sphere(origin, dir, radius_low);
    float t_enter = max(outer.x, 0.0);
    float t_exit = outer.y;
    if (r < radius_low)
    {
        t_enter = max(t_enter, inner.y);
    }
    else if (inner.x >= 0.0)
    {
        t_exit = min(t_exit, inner.x);
    }

    float2 earth = cloud_ray_sphere(origin, dir, cloud_earth_radius);
    if (r < cloud_earth_radius ||
        (earth.x >= 0.0 && dot(origin - cloud_earth_center, dir) < 0.0))
    {
        t_exit = min(t_exit, max(earth.x, 0.0));
    }
    return t_exit > t_enter ? float2(t_enter, t_exit) : float2(-1.0, -1.0);
}

// distance adaptive fine step, nearby clouds keep their detail, the far field takes long steps
float cloud_step_size(float t)
{
    float step_size = lerp(cumulus_step_min, cumulus_step_max, smoothstep(4000.0, 30000.0, t));
    return lerp(step_size, cumulus_step_far, saturate((t - 30000.0) / (cumulus_range_max - 30000.0)));
}

// Weather can avoid density/light work but cannot bound empty space, so empty space is probed
// with the low frequency density instead and only the intervals that hold a cloud are marched.
void cloud_march_cumulus(
    float3 cam_pos, float3 view_dir, float3 sun_dir,
    Texture3D noise_tex, Texture2D transmittance_lut, Texture2D sky_view_lut,
    SamplerState samp_noise, SamplerState samp_lut,
    float jitter, float max_distance,
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
    float phases_sun[cloud_scatter_octaves];
    cloud_scatter_phases(cos_th, phases_sun);
    
    const float density_thresh = 1e-4;
    float t = shell.x;
    
    float2 weather   = float2(0.0, 0.0);
    int weather_age  = cumulus_weather_interval;
    
    // moonlight is ~1e-6 of sunlight, its light march is invisible until the sun is about to set
    float3 moon_dir   = buffer_frame.celestial_moon.xyz;
    float cos_th_moon = dot(view_dir, moon_dir);
    bool march_moon   = sun_dir.y < 0.1 && buffer_frame.celestial_moon.w > 0.0;
    float phases_moon[cloud_scatter_octaves];
    cloud_scatter_phases(cos_th_moon, phases_moon);
    
    // shade fill, the sky from above and the lit ground from below, the ground reflects the sun
    // it receives through the gaps and the sky above it. the night sky comes in with the weight
    // the panorama gives it, so the clouds never fall below the sky they sit in
    float night_w        = sky_view_night_factor(sun_dir.y);
    float3 ambient_top   = cloud_sky_ambient(sky_view_lut, samp_lut) * cloud_ambient_sky;
    float3 ground_sun    = cloud_ground_albedo * (cloud_ambient_sky / PI) * saturate(sun_dir.y) * lerp(1.0, 0.3, cloud_coverage_cumulus());
    float3 ground_sky    = cloud_ground_albedo * ambient_top;
    
    // the light march is the expensive part of a sample, thin samples reuse the depth of the step before
    float sun_od      = 0.0;
    float moon_od     = 0.0;
    int   sun_od_age  = 2;
    int   moon_od_age = 2;
    
    bool probing    = true;
    int empty_steps = 0;
    
    [loop]
    for (int march_i = 0; march_i < cumulus_march_steps; march_i++)
    {
        if (transmittance < 0.01)
        {
            break;
        }
        
        float step_size = cloud_step_size(t);
        if (probing)
        {
            step_size = min(step_size * cumulus_probe_step_mul, step_size + cumulus_probe_step_add);
        }
        // Integrate only the remaining segment; jitter must not drop the final
        // interval or let it extend through geometry/the shell boundary.
        step_size = min(step_size, t_max - t);
        if (step_size <= 0.0) break;
        float sample_offset = frac(jitter + float(march_i) * 0.6180339887);
        float t_s = t + step_size * sample_offset;
        float3 pos = cam_pos + view_dir * t_s;
        
        if (probing)
        {
            weather = cloud_weather(noise_tex, samp_noise, pos);
            if (cloud_density_cumulus_cheap(pos, noise_tex, samp_noise, weather) > 0.0)
            {
                // a cloud is somewhere in this interval, march it again from its start in fine steps
                probing     = false;
                empty_steps = 0;
                weather_age = cumulus_weather_interval;
                continue;
            }
            t += step_size;
            if (t >= t_max)
            {
                break;
            }
            continue;
        }
        
        if (weather_age >= cumulus_weather_interval)
        {
            weather     = cloud_weather(noise_tex, samp_noise, pos);
            weather_age = 0;
        }
        weather_age++;
        
        float h_norm  = 0.0;
        float density = 0.0;
        if (weather.x > 0.0)
        {
            density = cloud_density_cumulus(pos, noise_tex, samp_noise, weather, h_norm);
            h_norm  = saturate(h_norm);
        }
        else
        {
            // zero coverage can end within a step, look again on the next one
            weather_age = cumulus_weather_interval;
        }
        
        if (density > density_thresh)
        {
            empty_steps = 0;
            
            float trans_before = transmittance;
            float ext         = density * cumulus_sigma_t;
            float step_trans  = exp(-ext * step_size);
            
            float light_jitter = frac(jitter + float(march_i) * 0.7548776662);
            int light_interval = density < cumulus_light_reuse_density ? 2 : 1;
            
            // Atmospheric transmittance already handles the local planetary horizon.
            float3 sun_light = cloud_sun_illuminance(pos, sun_dir, transmittance_lut, samp_lut);
            float3 direct    = 0.0;
            if (any(sun_light > 1e-7))
            {
                if (sun_od_age >= light_interval)
                {
                    sun_od     = cloud_sun_optical_depth_cumulus(pos, sun_dir, noise_tex, samp_noise, light_jitter);
                    sun_od_age = 0;
                }
                sun_od_age++;
                direct = sun_light * (cloud_scatter_attenuation(sun_od, phases_sun) * cloud_powder(density, cos_th));
            }
            
            if (march_moon)
            {
                float3 moon_light = cloud_sun_illuminance(pos, moon_dir, transmittance_lut, samp_lut) * cloud_moon_tint * buffer_frame.celestial_moon.w;
                if (any(moon_light > 1e-7))
                {
                    if (moon_od_age >= light_interval)
                    {
                        moon_od     = cloud_sun_optical_depth_cumulus(pos, moon_dir, noise_tex, samp_noise, light_jitter);
                        moon_od_age = 0;
                    }
                    moon_od_age++;
                    direct += moon_light * (cloud_scatter_attenuation(moon_od, phases_moon) * cloud_powder(density, cos_th_moon));
                }
            }
            
            // height inside the cloud, 0 at the base and 1 at the top of a cloud of this type
            float h_rel      = saturate(h_norm / cloud_top_norm(weather.y));
            float ground_w   = (1.0 - h_rel) * (1.0 - h_rel);
            float3 ambient   = ambient_top * lerp(cloud_ambient_sky_base, 1.0, h_rel);
            ambient         += (sun_light * ground_sun + ground_sky) * ground_w;
            
            // night ambient, sky fill locked to night_sky_radiance so clouds cannot fall below the exposed sky
            float3 ambient_night = night_sky_radiance(h_norm * 2.0 - 1.0) + night_airglow_rad;
            ambient             += ambient_night * (lerp(0.95, 1.20, h_norm) * night_w);
            
            float3 luminance_in = direct + ambient;
            float3 s_int        = cloud_albedo * luminance_in * (1.0 - step_trans);
            
            in_scatter    += transmittance * s_int;
            transmittance *= step_trans;
            float opacity = trans_before - transmittance;
            distance_weight += opacity * t_s;
            opacity_weight  += opacity;
        }
        else
        {
            // out of the cloud, the stored light depths belong to the cloud left behind
            sun_od_age  = 2;
            moon_od_age = 2;
            empty_steps++;
            probing = empty_steps >= cumulus_probe_resume;
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
    float jitter, float max_distance,
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
    float t      = shell.x;
    float cos_th = dot(view_dir, sun_dir);
    float phase  = cloud_phase(cos_th);
    
    [loop]
    for (int i = 0; i < cirrus_view_steps; i++)
    {
        if (transmittance < 0.01)
        {
            break;
        }
        
        float sample_offset = frac(jitter + float(i) * 0.6180339887);
        float t_s            = t + dt * sample_offset;
        float3 pos           = cam_pos + view_dir * t_s;
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
            
            float3 moon_dir   = buffer_frame.celestial_moon.xyz;
            float3 moon_light = cloud_sun_illuminance(pos, moon_dir, transmittance_lut, samp_lut) * cloud_moon_tint * buffer_frame.celestial_moon.w;
            float moon_phase  = cloud_hg_phase(dot(view_dir, moon_dir), 0.62);
            
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
            
            float3 s_int   = cloud_albedo * scat * (1.0 - step_trans);
            in_scatter    += transmittance * s_int;
            transmittance *= step_trans;
            float opacity = trans_before - transmittance;
            distance_weight += opacity * t_s;
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
    Texture3D noise_tex, Texture2D transmittance_lut, Texture2D sky_view_lut,
    SamplerState samp_noise, SamplerState samp_lut,
    float jitter, float max_distance,
    out float3 in_scatter, out float transmittance, out float representative_distance)
{
    in_scatter    = float3(0.0, 0.0, 0.0);
    transmittance = 1.0;
    representative_distance = 0.0;
    float distance_weight = 0.0;
    float opacity_weight  = 0.0;

    // Only cumulus is currently enabled; cirrus requires ordered layer integration.
    cloud_march_cumulus(cam_pos, view_dir, sun_dir,
        noise_tex, transmittance_lut, sky_view_lut,
        samp_noise, samp_lut,
        jitter, max_distance, in_scatter, transmittance, distance_weight, opacity_weight);
    if (opacity_weight <= 1e-5)
    {
        return;
    }

    // the march stops at 1 percent transmittance, stretch the rest to zero so a cloud that
    // thick is opaque instead of letting 1 percent of the sun disc behind it through
    transmittance = saturate((transmittance - 0.01) / 0.99);

    representative_distance = distance_weight / opacity_weight;
    // Approximate the cloud volume as one layer for aerial perspective. Applying
    // haze per march step changes Beer extinction and makes opacity step-dependent.
    // the haze in front of a cloud is the air's own glow, so the cloud fades into the radiance
    // of the atmosphere along the ray and keeps its opacity. fading the opacity instead let
    // whatever is behind the cloud through, a far cloud showed the sun disc, the moon and
    // the stars it covers
    float aerial_t = exp(-representative_distance * cloud_aerial_falloff);
    float3 air     = sky_view_radiance(sky_view_lut, samp_lut, view_dir, sun_dir);
    in_scatter     = in_scatter * aerial_t + air * ((1.0 - aerial_t) * (1.0 - transmittance));
}

// false when the scene along the ray ends before the cloud layer begins, the ground and
// everything standing on it, the passes skip their work for those pixels
bool cloud_ray_reaches_layer(float3 cam_pos, float3 view_dir, float max_distance)
{
    float2 shell = cloud_ray_shell(cam_pos,
        view_dir,
        cloud_earth_radius + cumulus_bottom_alt - cumulus_shell_padding,
        cloud_earth_radius + cumulus_top_alt    + cumulus_shell_padding);
    return shell.y >= 0.0 && shell.x < min(shell.y, min(cumulus_range_max, max_distance));
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

// farthest scene distance under a cloud pixel, which covers 2x2 render pixels. the march has to
// reach the farthest of them, stopping at the nearest left a cloud pixel that straddles a
// silhouette empty, which cut a clear fringe around foreground geometry and removed the clouds
// from sky gaps narrower than a cloud pixel. the composite rejects the cloud per render pixel
float cloud_scene_distance(float2 uv)
{
    float2 resolution = float2(get_render_resolution_active());
    float2 depth_uv   = saturate(cloud_depth_uv(uv, buffer_frame.taa_jitter_current));
    // top left texel of the 2x2 footprint, kept inside the active render area
    float2 pixel      = clamp(floor(depth_uv * resolution - 0.5), 0.0, resolution - 2.0);
    float2 texture_size;
    tex_depth.GetDimensions(texture_size.x, texture_size.y);
    // one gather reads the four depths, with reverse z the smallest of them is the farthest, so
    // a single position is reconstructed instead of four
    float4 raw  = tex_depth.GatherRed(GET_SAMPLER(sampler_point_clamp), (pixel + 1.0) / texture_size);
    float depth = min(min(raw.x, raw.y), min(raw.z, raw.w));
    if (depth <= 1e-6)
    {
        return 1e30;
    }
    return length(get_position(depth, (pixel + 1.0) / resolution) - get_camera_position());
}

float cloud_scene_distance_at(float2 uv, Texture2D depth_tex, float2 jitter, float3 camera_position)
{
    float2 depth_uv = saturate(cloud_depth_uv(uv, jitter));
    uint2 resolution = uint2(get_render_resolution_active());
    uint2 pixel = min(uint2(depth_uv * resolution), resolution - 1);
    float raw = depth_tex.Load(int3(pixel, 0)).r;
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
        // Tall cumulus still moves with cumulus wind. Altitude is not a layer ID.
        float wind_multiplier = cumulus_wind_speed_mul;
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

    uint hash    = cloud_hash_uint(pixel.x + cloud_hash_uint(pixel.y + cloud_hash_uint(buffer_frame.frame)));
    float jitter = float(hash) / 4294967295.0;
    float3 radiance;
    float transmittance;
    float representative_distance;
    clouds_evaluate_detailed(get_camera_position(), view_dir, sun_dir, tex3d, tex, tex2, GET_SAMPLER(sampler_bilinear_wrap), GET_SAMPLER(sampler_bilinear_clamp), jitter, max_distance, radiance, transmittance, representative_distance);

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
    float3 view_dir = cloud_view_direction(uv);
    if (!cloud_ray_reaches_layer(get_camera_position(), view_dir, cloud_scene_distance(uv)))
    {
        tex_uav[tid.xy]  = float4(0.0, 0.0, 0.0, 1.0);
        tex_uav2[tid.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    uint2 phase = uint2(
        buffer_frame.frame & 1u,
        (buffer_frame.frame >> 1u) & 1u
    );
    bool current_sample_valid = all((tid.xy & 1u) == phase);
    // only the marched pixel of a block holds a sample of this frame, the other three are rebuilt below
    float4 current         = float4(0.0, 0.0, 0.0, 1.0);
    float current_distance = 0.0;
    if (current_sample_valid)
    {
        current          = tex.Load(int3(pixel, 0));
        current_distance = tex2.Load(int3(pixel, 0)).r;
    }

    // one pixel of every 2x2 block is marched per frame, so this frame's samples sit on a lattice
    // two pixels apart and lattice is the step to the nearest one. the window reaches two pixels
    // out so it always holds 4 to 9 of them, a 3x3 window held a single sample for a marched
    // pixel, which collapsed the history clamp onto that sample and threw the accumulated
    // history away on every frame the pixel was marched
    int2 lattice = int2(phase) - int2(tid.xy & 1u);
    float4 neighborhood_min = 1e30;
    float4 neighborhood_max = -1e30;
    float4 reconstruction_sum = 0.0;
    float reconstruction_weight = 0.0;
    float distance_sum = 0.0;
    float distance_weight = 0.0;
    bool has_samples = false;
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            int2 offset = lattice + int2(x, y) * 2;
            int2 sample_pixel = pixel + offset;
            if (all(abs(offset) <= 2) && all(sample_pixel >= 0) && all(sample_pixel <= max_pixel))
            {
                float4 sample_value = tex.Load(int3(sample_pixel, 0));
                neighborhood_min = min(neighborhood_min, sample_value);
                neighborhood_max = max(neighborhood_max, sample_value);
                has_samples      = true;

                // the samples next to a pixel that was not marched fill it, the ring beyond them only
                // widens the clamp, so their distance is not read and the marched pixel reads none
                int offset_sq = dot(offset, offset);
                if (!current_sample_valid && offset_sq <= 2)
                {
                    float sample_distance = tex2.Load(int3(sample_pixel, 0)).r;
                    float weight = exp(-float(offset_sq));
                    float sample_opacity_weight = weight * saturate(1.0 - sample_value.a);
                    reconstruction_sum += sample_value * weight;
                    reconstruction_weight += weight;
                    if (sample_distance > 0.0)
                    {
                        distance_sum += sample_distance * sample_opacity_weight;
                        distance_weight += sample_opacity_weight;
                    }
                }
            }
        }
    }
    if (!current_sample_valid)
    {
        current = reconstruction_weight > 0.0 ? reconstruction_sum / reconstruction_weight : float4(0.0, 0.0, 0.0, 1.0);
        current_distance = distance_weight > 1e-6 ? distance_sum / distance_weight : 0.0;
    }

    float reprojection_distance = current_distance > 0.0 ? current_distance : 10000.0;
    float3 world_position = get_camera_position() + view_dir * reprojection_distance;
    float wind_multiplier = cumulus_wind_speed_mul;
    world_position.xz -= buffer_frame.wind.xz * wind_multiplier * buffer_frame.delta_time;
    matrix previous_vp = pass_is_right_eye() ? buffer_frame.view_projection_previous_unjittered_right : buffer_frame.view_projection_previous_unjittered;
    float4 previous_clip = mul(float4(world_position, 1.0), previous_vp);
    float2 previous_uv = ndc_to_uv(previous_clip.xy / max(previous_clip.w, 1e-6));
    float motion = saturate(length((previous_uv - uv) * float2(width, height)) * 0.125);

    // fast reprojection stays valid, the motion term below only shortens the history. rejecting
    // it above 4 pixels a frame dropped the clouds to the raw quarter-rate samples on every pan
    bool history_valid = !pass_bool(pass_clouds::reset_history) && previous_clip.w > 0.0 && all(previous_uv > 0.0) && all(previous_uv < 1.0);
    float4 history = current;
    float history_distance = current_distance;
    if (history_valid)
    {
        history          = tex3.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), previous_uv, 0);
        history_distance = tex4.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), previous_uv, 0).r;
        float expected_distance = length(world_position - buffer_frame.camera_position_previous);
        bool distance_valid = current_distance <= 0.0 || (history_distance > 0.0 && abs(history_distance - expected_distance) <= max(500.0, expected_distance * 0.2));

        float2 current_depth_uv = saturate(cloud_depth_uv(uv, buffer_frame.taa_jitter_current));
        // cloud uvs are screen uvs, the scene depths only fill the scaled render subrect
        float current_depth_raw = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), screen_uv_to_render_uv(current_depth_uv), 0).r;
        float2 previous_depth_uv = saturate(cloud_depth_uv(previous_uv, buffer_frame.taa_jitter_previous));
        float previous_depth_raw = tex5.SampleLevel(GET_SAMPLER(sampler_point_clamp), screen_uv_to_render_uv(previous_depth_uv), 0).r;
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
            previous_depth_raw = tex5.SampleLevel(GET_SAMPLER(sampler_point_clamp), screen_uv_to_render_uv(previous_depth_uv), 0).r;
            float expected_previous_depth = linearize_depth(scene_previous_clip.z / max(scene_previous_clip.w, 1e-6));
            float sampled_previous_depth  = linearize_depth(previous_depth_raw);
            depth_valid = scene_previous_clip.w > 0.0 && all(scene_previous_uv > 0.0) && all(scene_previous_uv < 1.0) && abs(sampled_previous_depth - expected_previous_depth) <= max(2.0, expected_previous_depth * 0.02);
        }
        history_valid = distance_valid && depth_valid && all(isfinite(history)) && isfinite(history_distance);
    }

    if (history_valid)
    {
        float distance_stability = smoothstep(6000.0, 25000.0, max(current_distance, history_distance));
        float4 neighborhood_range = neighborhood_max - neighborhood_min;
        float4 clamp_margin = neighborhood_range * lerp(0.25, 1.0, distance_stability);
        if (has_samples)
        {
            history = clamp(history, neighborhood_min - clamp_margin, neighborhood_max + clamp_margin);
        }
        float current_weight = lerp(lerp(0.06, 0.025, distance_stability), lerp(0.35, 0.12, distance_stability), motion);
        current_weight *= current_sample_valid ? 1.0 : lerp(0.35, 0.15, distance_stability);
        current = lerp(history, current, current_weight);
        current_distance = lerp(history_distance, current_distance, current_weight);
    }
    // Neighborhood margins can extend beyond physically valid radiance/transmittance.
    current.rgb = max(current.rgb, 0.0);
    current.a = saturate(current.a);
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
    uint2 resolution = uint2(get_render_resolution_active());
    if (any(tid.xy >= resolution))
    {
        return;
    }

    uint cloud_width;
    uint cloud_height;
    tex2.GetDimensions(cloud_width, cloud_height);
    // The scene is jittered, but the resolved cloud history is on an unjittered grid.
    // Sample clouds along the same ray as the scene pixel before temporal upscaling.
    float2 uv = (float2(tid.xy) + 0.5) / float2(resolution)
        - buffer_frame.taa_jitter_current * float2(0.5, -0.5);
    float scene_distance = cloud_scene_distance_at(uv, tex_depth, buffer_frame.taa_jitter_current, get_camera_position());
    float3 view_dir      = cloud_view_direction(uv);
    if (scene_distance < 1e20 && !cloud_ray_reaches_layer(get_camera_position(), view_dir, scene_distance))
    {
        tex_uav[tid.xy]  = tex.Load(int3(tid.xy, 0));
        tex_uav2[tid.xy] = tex4.Load(int3(tid.xy, 0));
        return;
    }

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
            float2 bilinear_axis = lerp(1.0 - cloud_fraction, cloud_fraction, float2(x, y));
            float bilinear_weight = bilinear_axis.x * bilinear_axis.y;
            // a tap is usable when its march reached this pixel's depth, taps that stopped at
            // nearer geometry hold no cloud for the space behind it
            float reach_weight = cloud_scene_distance(tap_uv) >= scene_distance * 0.98 ? 1.0 : 0.0;
            float visibility = cloud_distance <= 0.0 || cloud_distance < scene_distance ? 1.0 : 0.0;
            float weight = bilinear_weight * reach_weight * visibility;
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
        float2 velocity = cloud_velocity(view_dir, velocity_distance, has_cloud);
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

    // progressive strip bake
    uint2 pixel = tid.xy + uint2(0, pass_uint(pass_clouds::environment_row));
    if (any(pixel >= uint2(width, height)))
    {
        return;
    }

    float2 uv       = (float2(pixel) + 0.5) / float2(width, height);
    float3 view_dir = cloud_panorama_direction(uv);

    // the panorama is several times finer than this map, read the mip whose texels match so
    // the sun keeps its energy instead of being hit or missed by a point sample
    float sky_width;
    float sky_height;
    float sky_mips;
    tex.GetDimensions(0, sky_width, sky_height, sky_mips);
    float sky_mip    = clamp(log2(sky_width / float(width)), 0.0, sky_mips - 1.0);
    float3 clear_sky = tex.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv, sky_mip).rgb;

    float3 sun_dir = normalize(-light_parameters[0].direction);
    uint hash      = cloud_hash_uint(pixel.x + cloud_hash_uint(pixel.y));
    float jitter   = float(hash) / 4294967295.0;
    float3 radiance;
    float transmittance;
    float representative_distance;
    clouds_evaluate_detailed(get_camera_position(), view_dir, sun_dir, tex3d, tex2, tex3, GET_SAMPLER(sampler_bilinear_wrap), GET_SAMPLER(sampler_bilinear_clamp), jitter, 1e30, radiance, transmittance, representative_distance);
    tex_uav[pixel] = float4(clear_sky * transmittance + radiance, 1.0);
}
#endif

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
    
    // r, published perlin worley base shape
    float perlin_low = cloud_perlin_fbm(uvw, 4);
    float worley_low = cloud_worley_fbm(uvw, 4);
    float r = cloud_remap(
        perlin_low,
        1.0 - worley_low,
        1.0,
        0.0,
        1.0
    );
    
    // gba, increasing worley frequencies
    float g           = cloud_worley_fbm(uvw, 4);
    
    float b           = cloud_worley_fbm(uvw, 8);
    
    float a           = cloud_worley_fbm(uvw, 16);
    
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
    
    float trans = exp(-optical_depth * cumulus_sigma_t);
    tex_uav[tid.xy] = float4(trans, trans, trans, 1.0);
}
#endif

#endif // SPARTAN_CLOUDS
