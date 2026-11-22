/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "common.hlsl"
#include "restir_reservoir.hlsl"

// Reconstruction cannot estimate a surface absent from the low-resolution
// guides. Trace the existing independent path estimator at that surface instead
// of substituting black or unoccluded analytic lighting. The missing-sample mask
// depends only on geometry, not the random radiance draw.
[numthreads(8, 8, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    uint2 size;
    tex_uav.GetDimensions(size.x, size.y);
    if (any(pixel >= size))
        return;
    if (tex_uav[pixel].a >= 0.0f)
        return;

    float2 uv = restir_primary_uv(pixel, size);
    float3 position = restir_primary_position(uv);
    float3 normal = get_normal(uv);
    float3 geometric_normal = get_geometric_normal(uv);
    float4 material = get_material(uv);
    float3 albedo = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float roughness = max(material.r, 0.04f);
    float metallic = material.g;
    float3 view = normalize(get_camera_position() - position);
    uint2 block = pixel / 8u;
    restir_light_tile_index = xxhash32(block.x ^ xxhash32(block.y) ^ buffer_frame.frame) % RESTIR_LIGHT_TILE_COUNT;
    restir_load_primary_material(uv);
    PathSurface surface = restir_camera_primary(position, normal, geometric_normal, view, albedo, roughness, metallic);
    // These pixels bypass NRD entirely. Average independent paths to reduce flicker on thin edges.
    const uint sample_count = 4u;
    float3 radiance = 0.0f;
    bool smooth_dielectric = restir_is_smooth_dielectric_primary(surface);
    uint branch_count = smooth_dielectric ? 2u : 1u;
    [loop] for (uint i = 0; i < sample_count; ++i)
    {
        [loop] for (uint branch = 0u; branch < branch_count; ++branch)
        {
            uint seed = create_seed_for_pass(pixel, buffer_frame.frame, 7u + i * branch_count + branch);
            uint replay_seed = seed;
            float3 sample_radiance;
            trace_path_from_primary(surface, view, get_camera_position(), replay_seed, seed, sample_radiance, false, (PathSample)0, smooth_dielectric ? int(branch) : -1);
            radiance += max(sample_radiance, 0.0f) / float(sample_count);
        }
    }
    // Alpha zero is valid lighting but is not a denoised accumulation history.
    tex_uav[pixel] = float4(all(isfinite(radiance)) ? radiance : 0.0f, 0.0f);
}
