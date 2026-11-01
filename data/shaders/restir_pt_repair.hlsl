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
    uint seed = create_seed_for_pass(pixel, buffer_frame.frame, 7);
    uint2 block = pixel / 8u;
    restir_light_tile_index = xxhash32(block.x ^ xxhash32(block.y) ^ buffer_frame.frame) % RESTIR_LIGHT_TILE_COUNT;
    uint count = clamp(get_restir_initial_candidates(), 1u, 8u);
    float3 radiance = 0.0f;
    for (uint i = 0u; i < count; ++i)
    {
        uint replay_seed = seed;
        RestirSurfaceSample primary = restir_primary_sample(albedo, roughness, metallic, normal, geometric_normal,
            view, random_float2(seed), restir_primary_specular_blend(roughness));
        float pdf = restir_path_sample_pdf(primary);
        float3 direction = primary.direction;
        if (!primary.valid || !restir_pdf_valid(pdf) || dot(normal, direction) <= 0.0f || !all(isfinite(direction)))
            continue;
        float3 incident;
        trace_path_from_primary(position, normal, direction, pdf, primary.weight, replay_seed, seed, incident, false, (PathSample)0,
            view, albedo, roughness, metallic, get_camera_position(), geometric_normal);
        radiance += primary.weight * incident;
    }
    radiance /= float(count);
    RestirLightSample light = sample_direct_lighting_at_vertex(position, normal, geometric_normal,
        view, albedo, roughness, metallic, 1.0f, seed, false, 1u);
    radiance += light.radiance_sum;
    // Alpha zero is valid lighting but is not a denoised accumulation history.
    tex_uav[pixel] = float4(all(isfinite(radiance)) ? radiance : 0.0f, 0.0f);
}
