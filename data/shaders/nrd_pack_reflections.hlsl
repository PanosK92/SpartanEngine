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

// packs gbuffer guides + shaded reflections into nrd relax specular inputs
// tex       = reflections (rgb radiance, a hit distance)
// tex_uav   = in_mv (prev_uv - curr_uv, prev_view_z - curr_view_z)
// tex_uav2  = in_normal_roughness
// tex_uav3  = in_viewz
// tex_uav4  = in_spec_radiance_hitdist

// NRD assumes alpha = roughness squared. Convert our specular remap so its
// filtering and virtual-image reprojection follow the lobe actually traced.
float get_nrd_roughness(float2 uv)
{
    float coat;
    return sqrt(ggx_alpha_from_roughness(get_rt_reflection_roughness(uv, coat)));
}

// radiance shaded for pixel, read from where the shade pass stored it, which is the packed column under checkerboard
float4 pack_radiance(uint2 source, uint2 pixel, uint2 resolution, float denoising_range)
{
    if (pixel.x >= resolution.x)
    {
        return 0.0f;
    }

    const float2 uv = (pixel + 0.5f) / float2(resolution);
    if (!is_valid_render_uv(uv))
    {
        return 0.0f;
    }

    float depth  = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    float view_z = linearize_depth(depth);
    if (depth <= 0.0f || view_z >= denoising_range || get_rt_reflection_cached(uv) >= 1.0f)
    {
        return 0.0f;
    }

    float4 reflection = tex[source];
    float3 radiance   = max(reflection.rgb, 0.0f);
    float hit_dist    = max(reflection.a, 0.0f);
    return RELAX_FrontEnd_PackRadianceAndHitDist(radiance, hit_dist, true);
}

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

    const float denoising_range = max(buffer_frame.camera_far * 0.99f, 1.0f);

    // nrd reads the checkerboard signal from the left half only, the right half is left untouched
    bool checkerboard = pass_bool(pass_nrd_reflections::checkerboard);
    if (!checkerboard || thread_id.x < (active_resolution.x + 1u) / 2u)
    {
        uint2 pixel = checkerboard ? reflections_checkerboard_pixel(thread_id.xy) : thread_id.xy;
        tex_uav4[thread_id.xy] = pack_radiance(thread_id.xy, pixel, resolution, denoising_range);
    }

    const float2 uv = (thread_id.xy + 0.5f) / float2(resolution);
    float depth     = is_valid_render_uv(uv) ? tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r : 0.0f;
    float view_z    = linearize_depth(depth);
    // a pixel past the roughness cut carries no traced signal, nrd skips it the way it skips the sky
    if (depth <= 0.0f || view_z >= denoising_range || get_rt_reflection_cached(uv) >= 1.0f)
    {
        tex_uav[thread_id.xy]  = float4(0.0f, 0.0f, 0.0f, 0.0f);
        // pack a valid unit normal so nrd guide fetches stay finite even though view_z marks the texel out of range
        tex_uav2[thread_id.xy] = NRD_FrontEnd_PackNormalAndRoughness(float3(0.0f, 0.0f, 1.0f), 1.0f, 0.0f);
        tex_uav3[thread_id.xy] = float4(denoising_range * 2.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 normal_ws      = get_normal(uv);
    float4 velocity       = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float2 mv             = velocity.xy * float2(-0.5f, 0.5f);
    // NRD reconstructs previous view depth as view_z + mv.z; velocity.w stores signed previous view depth
    float previous_view_z = abs(velocity.w);
    if (!isfinite(previous_view_z) || previous_view_z <= 0.0f || previous_view_z >= FLT_MAX_16U)
    {
        float3 position_ws = get_position(depth, render_uv_to_screen_uv(uv));
        previous_view_z    = abs(mul(float4(position_ws, 1.0f), buffer_frame.view_previous).z);
    }
    float depth_motion = previous_view_z - view_z;

    tex_uav[thread_id.xy]  = float4(mv, depth_motion, 0.0f);
    tex_uav2[thread_id.xy] = NRD_FrontEnd_PackNormalAndRoughness(normal_ws, get_nrd_roughness(uv), 0.0f);
    tex_uav3[thread_id.xy] = float4(view_z, 0.0f, 0.0f, 0.0f);
}
