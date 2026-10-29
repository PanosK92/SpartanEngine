/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ====================
#include "common.hlsl"
#include "nrd/NRD.hlsli"
[[vk::image_format("unknown")]] RWTexture2D<float4> restir_specular_input : register(u68);
//===============================

// packs unified restir radiance + gbuffer guides into nrd reblur diffuse/specular inputs
// tex_uav  = in_mv (prev_uv - curr_uv)
// tex_uav2 = in_normal_roughness
// tex_uav3 = in_viewz
// tex_uav4 = in_diff_radiance_hitdist
// tex       = restir_output (combined diffuse and specular radiance)

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    if (any(thread_id.xy >= resolution))
    {
        return;
    }

    const float2 uv = (thread_id.xy + 0.5f) / float2(resolution);

    // Match the primary ray's point-sampled surface. Filtering depth blends unrelated
    // surfaces at edges; a world-space round trip also loses precision far from the origin.
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    float view_z = linearize_depth(depth);
    const float denoising_range = max(buffer_frame.camera_far * 0.99f, 1.0f);

    // sky, mark invalid for nrd, reverse-z clears far to 0
    if (depth <= 0.0f || view_z >= denoising_range)
    {
        tex_uav[thread_id.xy]  = float4(0.0f, 0.0f, 0.0f, 0.0f);
        // a raw zero here decodes to a degenerate normal, pack a unit one so nrd's guide
        // fetches stay finite even though the view z already marks the texel out of range
        tex_uav2[thread_id.xy] = NRD_FrontEnd_PackNormalAndRoughness(float3(0.0f, 0.0f, 1.0f), 1.0f, 0.0f);
        tex_uav3[thread_id.xy] = float4(denoising_range * 2.0f, 0.0f, 0.0f, 0.0f);
        tex_uav4[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        restir_specular_input[thread_id.xy] = 0.0f;
        return;
    }

    float3 normal_ws = get_normal(uv);
    float roughness  = max(get_material(uv).r, 0.04f);

    // engine velocity is unjittered ndc delta (curr - prev), nrd wants uv motion as prev = curr + mv
    float2 velocity_ndc = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).xy;
    float2 mv           = velocity_ndc * float2(-0.5f, 0.5f);

    float3 total_radiance = max(tex[thread_id.xy].rgb, 0.0f);
    float3 spec_radiance = clamp(tex2[thread_id.xy].rgb, 0.0f, total_radiance);
    float3 albedo = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float metallic = get_material(uv).g;
    float3 diffuse_factor = max(albedo * (1.0f - metallic), 0.02f);
    float3 specular_factor = max(lerp(0.04f, albedo, metallic), 0.02f);
    float3 diff_radiance = (total_radiance - spec_radiance) / diffuse_factor;
    spec_radiance /= specular_factor;
    // reconnection distance from the reuse passes, a real hit t lets reblur shrink kernels at contacts
    float hit_dist = max(tex[thread_id.xy].a, 0.0f);
    float3 hit_dist_params = float3(restir_hit_distance_a, restir_hit_distance_b, restir_hit_distance_c);
    float norm_hit_dist = REBLUR_FrontEnd_GetNormHitDist(hit_dist, view_z, hit_dist_params, 1.0f);

    tex_uav[thread_id.xy]  = float4(mv, 0.0f, 0.0f);
    tex_uav2[thread_id.xy] = NRD_FrontEnd_PackNormalAndRoughness(normal_ws, roughness, 0.0f);
    tex_uav3[thread_id.xy] = float4(view_z, 0.0f, 0.0f, 0.0f);
    tex_uav4[thread_id.xy] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(diff_radiance, norm_hit_dist, true);
    float spec_hit_dist = REBLUR_FrontEnd_GetNormHitDist(hit_dist, view_z, hit_dist_params, roughness);
    restir_specular_input[thread_id.xy] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(spec_radiance, spec_hit_dist, true);
}
