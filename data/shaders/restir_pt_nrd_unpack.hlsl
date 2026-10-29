/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ====================
#include "common.hlsl"
#include "nrd/NRD.hlsli"
//===============================

Texture2D<uint> tex_normal_previous : register(t87);

// combines remodulated nrd diffuse/specular output into restir_denoised
// tex     = out_diff_radiance_hitdist
// tex_uav = restir_denoised

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    if (any(thread_id.xy >= resolution))
    {
        return;
    }

    float2 uv = (float2(thread_id.xy) + 0.5f) / float2(resolution);
    if (any(thread_id.xy >= uint2(float2(resolution) * get_render_uv_scale())))
    {
        tex_uav[thread_id.xy] = 0.0f;
        return;
    }

    // Reconstruct the two lighting lobes before applying full-resolution material
    // detail. Remodulating at the tracing resolution bakes point-sampled albedo
    // into 4x4 blocks at 25%, which later filtering cannot recover.
    uint2 lighting_size;
    tex.GetDimensions(lighting_size.x, lighting_size.y);
    float2 position = uv * float2(lighting_size) - 0.5f;
    int2 base = int2(floor(position));
    float2 fraction = frac(position);
    int2 pixel_max = max(int2(float2(lighting_size) * get_render_uv_scale()) - 1, 0);
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
        tex_uav[thread_id.xy] = 0.0f;
        return;
    }
    float3 normal = get_normal(uv);
    float3 receiver = get_position(depth, render_uv_to_screen_uv(uv));
    float view_z = linearize_depth(depth);
    float3 diffuse = 0.0f;
    float3 specular = 0.0f;
    float weight_sum = 0.0f;
    float best_similarity = -1.0f;
    float3 best_diffuse = 0.0f;
    float3 best_specular = 0.0f;
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        int2 offset = int2(i & 1u, i >> 1u);
        int2 pixel = clamp(base + offset, int2(0, 0), pixel_max);
        float2 sample_uv = (float2(pixel) + 0.5f) / float2(lighting_size);
        // Match the actual G-buffer sample behind this low-resolution lighting tap.
        sample_uv = (floor(sample_uv * float2(resolution)) + 0.5f) / float2(resolution);
        float sample_depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), sample_uv, 0).r;
        if (sample_depth <= 0.0f)
            continue;
        float3 sample_normal = get_normal(sample_uv);
        float3 sample_position = get_position(sample_depth, render_uv_to_screen_uv(sample_uv));
        float3 separation = sample_position - receiver;
        // Plane distance keeps bilinear interpolation on sloped floors and walls;
        // raw view-depth differences reject neighbours on the very same plane.
        float plane_distance = max(abs(dot(separation, normal)), abs(dot(separation, sample_normal)));
        float similarity = exp(-plane_distance / max(view_z * 0.01f, 0.001f)) *
            pow(saturate(dot(normal, sample_normal)), 16.0f);
        float2 axis_weight = lerp(1.0f - fraction, fraction, float2(offset));
        float weight = axis_weight.x * axis_weight.y * similarity;
        float3 d = max(REBLUR_BackEnd_UnpackRadianceAndNormHitDist(tex.Load(int3(pixel, 0))).xyz, 0.0f);
        float3 s = max(REBLUR_BackEnd_UnpackRadianceAndNormHitDist(tex4.Load(int3(pixel, 0))).xyz, 0.0f);
        diffuse += d * weight;
        specular += s * weight;
        weight_sum += weight;
        if (similarity > best_similarity)
        {
            best_similarity = similarity;
            best_diffuse = d;
            best_specular = s;
        }
    }
    diffuse = weight_sum > 1e-5f ? diffuse / weight_sum : best_diffuse;
    specular = weight_sum > 1e-5f ? specular / weight_sum : best_specular;
    float3 albedo = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float metallic = get_material(uv).g;
    float3 radiance = diffuse * max(albedo * (1.0f - metallic), 0.02f) +
        specular * max(lerp(0.04f, albedo, metallic), 0.02f);
    float count = 1.0f;
    if (!pass_bool(pass_restir_nrd_unpack::reset_accumulation))
    {
        float4 history = tex2[thread_id.xy];
        float depth_prev = tex3.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
        float3 normal_prev = gbuffer_unpack_normal(tex_normal_previous[gbuffer_texel(uv)]);
        float2 motion = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).xy * buffer_frame.resolution_render * 0.5f;
        float z = linearize_depth(depth);
        bool valid = history.a >= 1.0f && all(isfinite(history)) && depth > 0.0f && depth_prev > 0.0f &&
            abs(linearize_depth(depth_prev) - z) < max(z * 0.01f, 0.001f) &&
            dot(normal, normal_prev) > 0.95f && dot(motion, motion) < 0.01f;
        if (valid)
        {
            // FP32 history avoids FP16 rounding stopping convergence after a few hundred frames.
            count = min(history.a + 1.0f, 4096.0f);
            radiance = lerp(history.rgb, radiance, 1.0f / count);
        }
    }
    tex_uav[thread_id.xy] = float4(radiance, count);
}
