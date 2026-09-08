#ifndef SPARTAN_FOG_MEDIUM
#define SPARTAN_FOG_MEDIUM
#include "fog_volume.hlsl"

float fog_noise(float3 p)
{
    float3 cell = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    float n = dot(cell, float3(1.0f, 57.0f, 113.0f));
    float4 a = frac(sin(n + float4(0.0f, 1.0f, 57.0f, 58.0f)) * 43758.5453f);
    float4 b = frac(sin(n + float4(113.0f, 114.0f, 170.0f, 171.0f)) * 43758.5453f);
    return lerp(lerp(lerp(a.x, a.y, f.x), lerp(a.z, a.w, f.x), f.y),
                lerp(lerp(b.x, b.y, f.x), lerp(b.z, b.w, f.x), f.y), f.z);
}

struct FogMedium
{
    float water;
    float air_extinction;
};

FogMedium fog_sample_medium(float3 position, float y0, float y1)
{
    FogMedium medium = (FogMedium)0;
    float terrain_valid;
    float terrain_y = sample_ocean_terrain_height(position.xz, terrain_valid);
    // Camera integration stops at opaque depth. Do not discard a whole cell
    // because its centre lies in the seabed: its front can still contain air
    // and illuminated water, especially in distant shoreline cells.

    float sea_level = buffer_frame.ocean_enabled > 0.5f ? buffer_frame.ocean_sea_level : 0.0f;
    float ground_y = terrain_valid > 0.5f ? max(terrain_y, sea_level) : sea_level;
    float above_ground = max(position.y - ground_y, 0.0f);
    float shelter = 0.0f;
    if (terrain_valid > 0.5f && buffer_frame.terrain_maps_enabled > 0.5f)
    {
        float2 terrain_uv = (position.xz - buffer_frame.terrain_height_mapping.xy) * buffer_frame.terrain_height_mapping.zw;
        float4 analysis = tex_terrain_map_a.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), terrain_uv, 0.0f);
        shelter = saturate(analysis.b * 0.6f + analysis.g * 0.25f + saturate(analysis.r * 2.0f - 1.0f) * 0.35f);
    }

    float height_scale = max(pass_get_f3_value2().x, 1.0f);
    float ground_amount = max(pass_get_f3_value2().y, 0.0f);
    float breakup = saturate(pass_get_f3_value2().z);
    float3 advected = position - buffer_frame.wind * (float(buffer_frame.time) * 0.08f);
    float noise = fog_noise(advected * float3(0.008f, 0.022f, 0.008f));
    noise = noise * 0.7f + fog_noise(advected * 0.031f) * 0.3f;
    float structure = lerp(1.0f, smoothstep(0.18f, 0.82f, noise) * 1.8f, breakup);
    float wind_mixing = rcp(1.0f + length(buffer_frame.wind) * (1.0f - shelter) * 0.06f);
    float air = max(pass_get_f3_value().y, 0.0f) * (
        0.00025f * exp(-max(position.y - sea_level, 0.0f) / height_scale)
        + 0.0012f * ground_amount * exp(-above_ground / 35.0f)
            * (0.25f + shelter * 1.75f) * wind_mixing * structure);

    if (buffer_frame.ocean_enabled > 0.5f)
    {
        float water_y = sea_level;
        // FFT work is only needed in the wave band. Deep samples still receive
        // the moving entry point when their sunlight is evaluated.
        if (abs(position.y - sea_level) < 32.0f)
            water_y = get_ocean_height(position.xz);
        medium.water = abs(y1 - y0) > 1e-5f
            ? saturate((water_y - min(y0, y1)) / abs(y1 - y0))
            : (position.y < water_y ? 1.0f : 0.0f);
    }

    medium.air_extinction = air;
    return medium;
}
#endif
