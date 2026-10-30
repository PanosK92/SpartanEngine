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
    // Lighting is traced at snapped full-resolution guide texel centres, not
    // the nominal low-resolution centres. At 25% these differ by half a
    // full-resolution pixel. Bracket the receiver using the actual centres
    // before computing interpolation weights (also handles noninteger scales).
    float2 guide_ratio = float2(resolution) / float2(lighting_size);
    float2 receiver_pixel = uv * float2(resolution);
    float2 lower_center = floor((float2(base) + 0.5f) * guide_ratio) + 0.5f;
    base -= int2(receiver_pixel.x < lower_center.x ? 1 : 0,
                 receiver_pixel.y < lower_center.y ? 1 : 0);
    lower_center = floor((float2(base) + 0.5f) * guide_ratio) + 0.5f;
    float2 upper_center = floor((float2(base) + 1.5f) * guide_ratio) + 0.5f;
    float2 fraction = saturate((receiver_pixel - lower_center) / max(upper_center - lower_center, 1.0f));
    int2 pixel_max = max(int2(float2(lighting_size) * get_render_uv_scale()) - 1, 0);
    // Match packing's validity range. NRD does not guarantee writes outside
    // this range, so those output texels can still contain an older surface.
    const float denoising_range = max(buffer_frame.camera_far * 0.99f, 1.0f);
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f || linearize_depth(depth) >= denoising_range)
    {
        tex_uav[thread_id.xy] = 0.0f;
        return;
    }
    float3 normal = get_normal(uv);
    float3 receiver = get_position(depth, render_uv_to_screen_uv(uv));
    float view_z = linearize_depth(depth);
    float4 material = get_material(uv);
    float roughness = sqrt(ggx_alpha_from_roughness(max(material.r, 0.04f)));
    float3 view_dir = normalize(get_camera_position() - receiver);
    float3 reflection_dir = reflect(-view_dir, normal);
    float3 diffuse = 0.0f;
    float3 specular = 0.0f;
    float weight_sum = 0.0f;
    float specular_weight_sum = 0.0f;
    float best_similarity = -1.0f;
    float best_specular_similarity = -1.0f;
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
        if (sample_depth <= 0.0f || linearize_depth(sample_depth) >= denoising_range)
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
        // Demodulation removes material amplitude, not reflection lobe width.
        // Coplanar glossy/rough surfaces must not share specular interpolation
        // weights. Use NRD's roughness convention and its default 15% fraction.
        float sample_roughness = sqrt(ggx_alpha_from_roughness(max(get_material(sample_uv).r, 0.04f)));
        float roughness_weight = exp(-abs(sample_roughness - roughness) / max(0.15f * roughness, 0.01f));
        // A broad diffuse normal gate cannot resolve a narrow reflection lobe.
        // Use a GGX-shaped angular weight, normalized to one at zero separation.
        // Reflection rotates twice as fast as its microfacet normal, hence the
        // half-angle cosine squared. Include view changes across grazing planes.
        float3 sample_view = normalize(get_camera_position() - sample_position);
        float3 sample_reflection = reflect(-sample_view, sample_normal);
        float half_cos_sq = saturate(0.5f + 0.5f * dot(reflection_dir, sample_reflection));
        float alpha_sq = roughness * roughness * roughness * roughness;
        float lobe_ratio = alpha_sq / max(1.0f - half_cos_sq + alpha_sq * half_cos_sq, 1e-8f);
        float lobe_weight = lobe_ratio * lobe_ratio;
        float specular_similarity = similarity * roughness_weight * lobe_weight;
        float specular_weight = weight * roughness_weight * lobe_weight;
        float3 d = max(REBLUR_BackEnd_UnpackRadianceAndNormHitDist(tex.Load(int3(pixel, 0))).xyz, 0.0f);
        float3 s = max(REBLUR_BackEnd_UnpackRadianceAndNormHitDist(tex4.Load(int3(pixel, 0))).xyz, 0.0f);
        diffuse += d * weight;
        specular += s * specular_weight;
        weight_sum += weight;
        specular_weight_sum += specular_weight;
        if (similarity > best_similarity)
        {
            best_similarity = similarity;
            best_diffuse = d;
        }
        if (specular_similarity > best_specular_similarity)
        {
            best_specular_similarity = specular_similarity;
            best_specular = s;
        }
    }
    diffuse = weight_sum > 1e-5f ? diffuse / weight_sum : best_diffuse;
    specular = specular_weight_sum > 1e-5f ? specular / specular_weight_sum : best_specular;
    float3 albedo = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float metallic = material.g;
    // Match packing's GGX-to-NRD conversion using the original material guide,
    // not NRD's filtered roughness. Restore shading detail at render resolution.
    float3 diffuse_factor, specular_factor;
    NRD_MaterialFactors(normal, view_dir, albedo * (1.0f - metallic),
        lerp(0.04f, albedo, metallic), roughness, diffuse_factor, specular_factor);
    float3 radiance = diffuse * diffuse_factor + specular * specular_factor;
    float count = 1.0f;
    if (!pass_bool(pass_restir_nrd_unpack::reset_accumulation))
    {
        float4 history = tex2[thread_id.xy];
        float depth_prev = tex3.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
        float3 normal_prev = gbuffer_unpack_normal(tex_normal_previous[gbuffer_texel(uv)]);
        float2 motion = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).xy * buffer_frame.resolution_render * 0.5f;
        float z = linearize_depth(depth);
        // Accumulation is restricted to a stationary camera (or subpixel breathing).
        // The same history pixel lies on a different ray after TAA jitter. Compare
        // surface planes, not view depths: on a distant grazing floor a fraction
        // of a pixel can change view depth by more than the old one-percent gate.
        float2 jitter_delta = (buffer_frame.taa_jitter_previous - buffer_frame.taa_jitter_current) *
            float2(0.5f, -0.5f) * get_render_uv_scale();
        float3 previous_receiver = get_position(depth_prev, render_uv_to_screen_uv(uv - jitter_delta));
        float3 separation = previous_receiver - receiver;
        float plane_distance = max(abs(dot(separation, normal)), abs(dot(separation, normal_prev)));
        bool valid = history.a >= 1.0f && all(isfinite(history)) && depth > 0.0f && depth_prev > 0.0f &&
            plane_distance < max(z * 0.001f, 0.002f) &&
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
