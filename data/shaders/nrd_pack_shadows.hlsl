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

// packs guides + rt shadow (shadow_mask_pack layout) into sigma penumbra and translucency
// pass_nrd_shadows::tan_light_angular_radius = for the sun
// pass_nrd_shadows::local_slice              = local array slice
// pass_nrd_shadows::is_local                 = 0 sun, 1 local light
// pass_nrd_shadows::light_index              = the local light, its distance and size size the penumbra
// tex       = ray_traced_shadows
// tex_uav   = in_mv
// tex_uav2  = in_normal_roughness
// tex_uav3  = in_viewz
// tex_uav4  = in_penumbra (r32f)
// tex_uav5  = in_translucency (rgba16f, borrowed from the reflections denoiser, which runs later on the graphics queue)

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    uint2 active_resolution = max(uint2(floor(float2(resolution) * buffer_frame.resolution_scale)), uint2(1u, 1u));
    if (any(thread_id.xy >= active_resolution))
    {
        return;
    }

    const float2 uv = (thread_id.xy + 0.5f) / float2(resolution);
    // uv sample so half-res sigma can read full-res gbuffer depth
    float depth = get_depth(uv);
    float view_z = abs(get_position_view_space(uv).z);
    const float denoising_range = max(buffer_frame.camera_far * 0.99f, 1.0f);
    const float tan_light_angular_radius = pass_float(pass_nrd_shadows::tan_light_angular_radius);
    const uint  local_slice = pass_uint(pass_nrd_shadows::local_slice);
    const bool  is_local    = pass_bool(pass_nrd_shadows::is_local);

    if (depth <= 0.0f || view_z >= denoising_range)
    {
        if (!is_local)
        {
            tex_uav[thread_id.xy]  = float4(0.0f, 0.0f, 0.0f, 0.0f);
            tex_uav2[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            tex_uav3[thread_id.xy] = float4(denoising_range * 2.0f, 0.0f, 0.0f, 0.0f);
        }
        tex_uav4[thread_id.xy] = float4(NRD_FP16_MAX, 0.0f, 0.0f, 0.0f);
        tex_uav5[thread_id.xy] = SIGMA_FrontEnd_PackTranslucency(NRD_FP16_MAX, 1.0f);
        return;
    }

    // The sun pack runs first and all SIGMA lights share the same camera
    // guides. Local lights only replace the penumbra signal; NRD reads the
    // guides without modifying them.
    if (!is_local)
    {
        float3 normal_ws = get_normal(uv);
        float2 velocity_ndc = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).xy;
        float2 mv = velocity_ndc * float2(-0.5f, 0.5f);
        tex_uav[thread_id.xy]  = float4(mv, 0.0f, 0.0f);
        tex_uav2[thread_id.xy] = NRD_FrontEnd_PackNormalAndRoughness(normal_ws, 1.0f, 0.0f);
        tex_uav3[thread_id.xy] = float4(view_z, 0.0f, 0.0f, 0.0f);
    }

    float4 shadow       = is_local ? tex_rt_shadows_local.Load(int4(thread_id.xy, local_slice, 0)) : tex[thread_id.xy];
    float3 visibility   = shadow_mask_unpack(shadow);
    float blocker_dist  = shadow.g;
    float dist_to_light = 1.0f;
    float light_size    = 1.0f;
    if (is_local)
    {
        // the same distance and size the trace measured against, see trace_local_light_shadow
        LightParameters light = light_parameters[pass_uint(pass_nrd_shadows::light_index)];
        float3 position       = get_position(uv);
        float3 target         = light.position;
        light_size            = 0.1f;
        if ((light.flags & (1u << 6)) != 0u)
        {
            float3 right   = normalize(light.direction_right);
            float3 up      = normalize(cross(light.direction, right));
            float3 offset  = position - light.position;
            target        += right * clamp(dot(offset, right), -light.area_width * 0.5f, light.area_width * 0.5f);
            target        += up * clamp(dot(offset, up), -light.area_height * 0.5f, light.area_height * 0.5f);
            light_size     = max(light.area_width, light.area_height);
        }
        dist_to_light = max(length(target - position), 1e-3f);
        light_size    = max(light_size, 1e-3f);
    }

    // The shadow ray already offsets its origin. Close hits are real blockers
    // (for example car trim and tire contacts), not evidence of an unoccluded ray.
    // A pixel dimmed only by glass carries the distance to the glass, it's packed as blocked there so
    // the opaque shadow (x) reads zero and the colour comes through the translucency signal (yzw).
    // A pixel with no blocker distance is a receiver facing away from the light, the lighting pass
    // zeroes it through n dot l, it's sent as lit so it doesn't bleed into the lit side.
    bool lit = all(visibility >= 0.99f) || blocker_dist <= 0.0f;
    float distance_to_occluder = lit ? NRD_FP16_MAX : blocker_dist;

    // an occluder touching the emitter divides by zero inside the penumbra pack
    if (is_local && distance_to_occluder < NRD_FP16_MAX)
    {
        distance_to_occluder = min(distance_to_occluder, dist_to_light * 0.95f);
    }

    float penumbra = is_local
        ? SIGMA_FrontEnd_PackPenumbra(distance_to_occluder, dist_to_light, light_size)
        : SIGMA_FrontEnd_PackPenumbra(distance_to_occluder, tan_light_angular_radius);

    tex_uav4[thread_id.xy] = float4(penumbra, 0.0f, 0.0f, 0.0f);
    // 0 behind an opaque blocker, the glass colour behind glass, a lit pixel ignores it
    tex_uav5[thread_id.xy] = SIGMA_FrontEnd_PackTranslucency(distance_to_occluder, lit ? float3(1.0f, 1.0f, 1.0f) : visibility);
}
