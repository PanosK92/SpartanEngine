#ifndef SPARTAN_FOG_VOLUME
#define SPARTAN_FOG_VOLUME
#include "common.hlsl"
#include "shared_fog.h"

struct FogTransport
{
    float3 scattering;
    float3 transmittance;
};

float3 fog_view_direction(float2 uv)
{
    float4 view = mul(float4(uv_to_ndc(uv), 1.0f, 1.0f), get_projection_inverted());
    return normalize(mul(float4(normalize(view.xyz / view.w), 0.0f), get_view_inverted()).xyz);
}

float3 fog_froxel_world(float3 voxel)
{
    float2 uv = (voxel.xy + 0.5f) / float2(fog_width, fog_height);
    return get_camera_position() + fog_view_direction(uv)
        * fog_slice_to_distance((voxel.z + 0.5f) / float(fog_depth));
}

// Integrated textures store slice BOUNDARIES, including identity at the camera.
// Interpolate in metres within a cell, not logarithmic slice coordinates.
FogTransport sample_fog_ray(float2 uv, float distance_camera)
{
    float slice = min(fog_distance_to_slice(distance_camera) * float(fog_depth), float(fog_depth) - 0.0001f);
    float index = floor(slice);
    float d0 = fog_slice_to_distance(index / float(fog_depth));
    float d1 = fog_slice_to_distance((index + 1.0f) / float(fog_depth));
    float fraction = saturate((distance_camera - d0) / max(d1 - d0, 1e-5f));
    float3 coord0 = float3(uv, (index + 0.5f) / float(fog_depth + 1u));
    float3 s0 = tex_fog_scattering.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), coord0, 0.0f).rgb;
    float3 t0 = saturate(tex_fog_transmittance.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), coord0, 0.0f).rgb);
    float2 material = tex_fog_extinction.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp),
        float3(uv, (index + 0.5f) / float(fog_depth)), 0.0f).rg;
    float air = max(material.r, 0.0f);
    float water_fraction = saturate(material.g);
    float3 water_extinction = get_ocean_extinction();
    float3 material_uv = float3(uv, (index + 0.5f) / float(fog_depth));
    float3 air_source = tex_fog_air_source.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), material_uv, 0.0f).rgb;
    float3 water_source = tex_fog_water_source.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), material_uv, 0.0f).rgb;
    bool water_first = fog_view_direction(uv).y > 0.0f;
    float3 weight, partial_t;
    [unroll] for (uint channel = 0u; channel < 3u; channel++)
    {
        weight[channel] = fog_layered_weight(air, water_extinction[channel], air_source[channel],
            water_source[channel], d1 - d0, water_fraction, water_first, (d1 - d0) * fraction);
        partial_t[channel] = fog_layered_transmittance(air, water_extinction[channel],
            d1 - d0, water_fraction, water_first, (d1 - d0) * fraction);
    }
    FogTransport result;
    result.scattering = s0 + t0 * weight;
    result.transmittance = t0 * partial_t;
    return result;
}

FogTransport sample_fog_volume(float2 uv, float distance_camera)
{
    float3 ray = fog_view_direction(uv);
    float y = get_camera_position().y + ray.y * distance_camera;
    if (buffer_frame.ocean_enabled < 0.5f || distance_camera < fog_detail_far ||
        abs(y - buffer_frame.ocean_sea_level) > 32.0f || abs(ray.y) < 0.02f)
        return sample_fog_ray(uv, distance_camera);

    // Reconstruct transport before filtering it. Mixing interface fractions
    // before exponentiation invents absorbing water above the surface. Sample
    // adjacent columns on the same horizontal plane to preserve that boundary.
    float2 pixel = uv * float2(fog_width, fog_height) - 0.5f;
    float2 base = floor(pixel);
    float2 blend = frac(pixel);
    FogTransport result = (FogTransport)0;
    [unroll] for (uint tap = 0u; tap < 4u; tap++)
    {
        float2 offset = float2(tap & 1u, tap >> 1u);
        float2 tap_uv = (clamp(base + offset, 0.0f, float2(fog_width - 1u, fog_height - 1u)) + 0.5f)
            / float2(fog_width, fog_height);
        float tap_y = fog_view_direction(tap_uv).y;
        float distance_tap = fog_reproject_height_distance(distance_camera, ray.y, tap_y);
        FogTransport value = sample_fog_ray(tap_uv, distance_tap);
        float weight = (offset.x > 0.5f ? blend.x : 1.0f - blend.x)
            * (offset.y > 0.5f ? blend.y : 1.0f - blend.y);
        result.scattering += value.scattering * weight;
        result.transmittance += value.transmittance * weight;
    }
    return result;
}

// Recover only the segment behind a transparent surface. Its foreground segment
// is applied once, after reflections, IBL, water and surface foam are composed.
float3 fog_transmit_segment(float3 radiance, float2 uv, float start_distance, float end_distance)
{
    FogTransport front = sample_fog_volume(uv, start_distance);
    FogTransport back = sample_fog_volume(uv, max(start_distance, end_distance));
    return (radiance * min(back.transmittance, front.transmittance)
        + max(back.scattering - front.scattering, 0.0f)) / max(front.transmittance, 1e-5f);
}
#endif
