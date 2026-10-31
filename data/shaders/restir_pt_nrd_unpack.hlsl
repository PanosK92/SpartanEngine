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

Texture2D<uint2> tex_normal_previous : register(t87);

// combines remodulated nrd diffuse/specular output into restir_denoised
// tex     = out_diff_radiance_hitdist
// tex_uav = restir_denoised
// Positive alpha stores integer history count + half the NRD roughness.
// FP32 retains roughness within 0.001 even at the 4096-frame count limit.

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
    float2 receiver_pixel = uv * float2(resolution);
    float2 lower_center = restir_guide_center(base, lighting_size, resolution);
    base -= int2(receiver_pixel.x < lower_center.x ? 1 : 0,
                 receiver_pixel.y < lower_center.y ? 1 : 0);
    lower_center = restir_guide_center(base, lighting_size, resolution);
    float2 upper_center = restir_guide_center(base + 1, lighting_size, resolution);
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
    float weight_scale = 0.0f;
    float specular_weight_scale = 0.0f;
    float best_similarity = -1.0f;
    float best_specular_similarity = -1.0f;
    float3 best_diffuse = 0.0f;
    float3 best_specular = 0.0f;
    // Search the surrounding ring only when the bilinear footprint has no
    // compatible guide. Thin edges can miss all four taps at reduced resolution
    // even though a matching traced surface exists just outside that footprint.
    const int2 reconstruction_offsets[16] = {
        int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1),
        int2(-1, -1), int2(0, -1), int2(1, -1), int2(2, -1),
        int2(-1, 0), int2(2, 0), int2(-1, 1), int2(2, 1),
        int2(-1, 2), int2(0, 2), int2(1, 2), int2(2, 2)
    };
    [loop]
    for (uint i = 0; i < 16; ++i)
    {
        if (i == 4 && best_similarity >= 0.0f)
            break;
        int2 offset = reconstruction_offsets[i];
        int2 pixel = clamp(base + offset, int2(0, 0), pixel_max);
        // Match the actual G-buffer sample behind this low-resolution lighting tap.
        float2 sample_uv = (float2(restir_guide_pixel(uint2(pixel), lighting_size, resolution)) + 0.5f) / float2(resolution);
        float sample_depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), sample_uv, 0).r;
        if (sample_depth <= 0.0f || linearize_depth(sample_depth) >= denoising_range)
            continue;
        float3 sample_normal = get_normal(sample_uv);
        float3 sample_position = get_position(sample_depth, render_uv_to_screen_uv(sample_uv));
        float3 separation = sample_position - receiver;
        // Plane distance keeps bilinear interpolation on sloped floors and walls;
        // raw view-depth differences reject neighbours on the very same plane.
        float plane_distance = max(abs(dot(separation, normal)), abs(dot(separation, sample_normal)));
        // Soft weights alone cannot reject a footprint containing only other
        // surfaces: normalization (or the fallback) restores their full energy.
        // Match the spatial-reuse surface tolerances before interpolating.
        if (plane_distance > max(view_z * 0.03f, 0.005f) || dot(normal, sample_normal) < 0.9f)
            continue;
        float similarity = exp(-plane_distance / max(view_z * 0.01f, 0.001f)) *
            pow(saturate(dot(normal, sample_normal)), 16.0f);
        float2 axis_weight = lerp(1.0f - fraction, fraction, float2(offset));
        // Ring taps use positive distance weights, never extrapolated bilinear
        // weights. Keep the same surface rejection and per-lobe weights below.
        float2 tap_distance = float2(offset) - fraction;
        float spatial_weight = i < 4 ? axis_weight.x * axis_weight.y :
            rcp(1.0f + dot(tap_distance, tap_distance));
        float weight = spatial_weight * similarity;
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
        // Normalize relative weights, not their absolute magnitude. Narrow
        // reflection lobes can make every weight tiny without making their
        // ratios meaningless. Keep each running sum scaled by its largest tap
        // to avoid underflow and an arbitrary switch to a single-tap fallback.
        if (weight > 0.0f)
        {
            float scale = max(weight_scale, weight);
            float rescale = weight_scale / scale;
            float relative_weight = weight / scale;
            diffuse = diffuse * rescale + d * relative_weight;
            weight_sum = weight_sum * rescale + relative_weight;
            weight_scale = scale;
        }
        if (specular_weight > 0.0f)
        {
            float scale = max(specular_weight_scale, specular_weight);
            float rescale = specular_weight_scale / scale;
            float relative_weight = specular_weight / scale;
            specular = specular * rescale + s * relative_weight;
            specular_weight_sum = specular_weight_sum * rescale + relative_weight;
            specular_weight_scale = scale;
        }
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
    if (best_similarity < 0.0f)
    {
        // Negative alpha marks a missing reconstruction, distinct from valid
        // black lighting or the raw fallback's nonnegative hit distance.
        tex_uav[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, -1.0f);
        return;
    }
    diffuse = weight_sum > 0.0f ? diffuse / weight_sum : best_diffuse;
    specular = specular_weight_sum > 0.0f ? specular / specular_weight_sum : best_specular;
    float3 albedo = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float metallic = material.g;
    // Match packing's GGX-to-NRD conversion using the original material guide,
    // not NRD's filtered roughness. Restore shading detail at render resolution.
    float3 diffuse_factor, specular_factor;
    NRD_MaterialFactors(normal, view_dir, albedo * (1.0f - metallic),
        lerp(0.04f, albedo, metallic), roughness, diffuse_factor, specular_factor);
    float3 radiance = diffuse * diffuse_factor + specular * specular_factor;
    float count = 1.0f;
    // This extra still-camera average reads the same texel, unlike NRD's
    // reprojected history. A changed render rectangle changes that texel's ray.
    if (!pass_bool(pass_restir_nrd_unpack::reset_accumulation) &&
        all(get_render_uv_scale() == get_render_uv_scale_previous()))
    {
        float4 history = tex2[thread_id.xy];
        float history_count = floor(history.a);
        float previous_roughness = frac(history.a) * 2.0f;
        float depth_prev = tex3.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
        float3 normal_prev = gbuffer_unpack_normal(tex_normal_previous[gbuffer_texel(uv)]);
        // Velocity is in screen NDC; the tolerance below is in active render
        // pixels, not the larger backing texture used by resolution scaling.
        float2 motion = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).xy * get_render_resolution_active() * 0.5f;
        float z = linearize_depth(depth);
        // Accumulation is restricted to a stationary camera (or subpixel breathing).
        // The same history pixel lies on a different ray after TAA jitter. Compare
        // surface planes, not view depths: on a distant grazing floor a fraction
        // of a pixel can change view depth by more than the old one-percent gate.
        float2 jitter_delta = (buffer_frame.taa_jitter_previous - buffer_frame.taa_jitter_current) *
            float2(0.5f, -0.5f) * get_render_uv_scale();
        // The projection is unchanged while accumulation is enabled, apart
        // from jitter, but camera breathing can still change the view transform.
        // Unproject into view space, then use the previous camera's rigid inverse
        // instead of interpreting the old depth through the current camera pose.
        float2 previous_screen_uv = render_uv_to_screen_uv(uv - jitter_delta);
        float4 previous_clip = float4(previous_screen_uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), depth_prev, 1.0f);
        float4 previous_view = mul(previous_clip, get_projection_inverted());
        float3 previous_receiver = mul(previous_view.xyz / previous_view.w,
            transpose((float3x3)buffer_frame.view_previous)) + buffer_frame.camera_position_previous;
        float3 separation = previous_receiver - receiver;
        float plane_distance = max(abs(dot(separation, normal)), abs(dot(separation, normal_prev)));
        // The broad surface-normal gate is insufficient for glossy lighting.
        // Jitter and camera breathing can change reflection direction while
        // passing it, freezing a different reflection in the long average.
        float3 previous_view_dir = normalize(buffer_frame.camera_position_previous - previous_receiver);
        float3 previous_reflection = reflect(-previous_view_dir, normal_prev);
        float reflection_half_cos_sq = saturate(0.5f + 0.5f * dot(reflection_dir, previous_reflection));
        float alpha_sq = roughness * roughness * roughness * roughness;
        // tan^2(half angle) <= alpha^2: use the GGX characteristic slope width.
        bool reflection_matches = 1.0f - reflection_half_cos_sq <= alpha_sq * reflection_half_cos_sq;
        // Same geometry can have different lobe widths under subpixel jitter.
        // Match the roughness tolerance used by spatial reconstruction.
        bool roughness_matches = previous_roughness > 0.0f &&
            abs(previous_roughness - roughness) <= max(0.15f * roughness, 0.01f);
        bool valid = history_count >= 1.0f && all(isfinite(history)) && depth > 0.0f && depth_prev > 0.0f &&
            plane_distance < max(z * 0.001f, 0.002f) &&
            dot(normal, normal_prev) > 0.95f && dot(motion, motion) < 0.01f && reflection_matches && roughness_matches;
        if (valid)
        {
            // FP32 history avoids FP16 rounding stopping convergence after a few hundred frames.
            count = min(history_count + 1.0f, 4096.0f);
            radiance = lerp(history.rgb, radiance, 1.0f / count);
        }
    }
    tex_uav[thread_id.xy] = float4(radiance, count + 0.5f * saturate(roughness));
}
