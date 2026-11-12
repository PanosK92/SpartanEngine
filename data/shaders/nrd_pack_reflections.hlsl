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

// packs gbuffer guides + shaded reflections into nrd reblur specular inputs
// tex       = reflections (rgb radiance, a hit distance)
// tex_uav   = in_mv
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
    float view_z    = abs(get_position_view_space(uv).z);
    if (get_depth(uv) <= 0.0f || view_z >= denoising_range || get_rt_reflection_cached(uv) >= 1.0f)
    {
        return 0.0f;
    }

    float4 reflection      = tex[source];
    float3 radiance        = max(reflection.rgb, 0.0f);
    float hit_dist         = max(reflection.a, 0.0f);
    float3 hit_dist_params = float3(3.0f, 0.1f, 20.0f);
    float norm_hit_dist    = REBLUR_FrontEnd_GetNormHitDist(hit_dist, view_z, hit_dist_params, get_nrd_roughness(uv));
    return REBLUR_FrontEnd_PackRadianceAndNormHitDist(radiance, norm_hit_dist, true);
}

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    if (any(thread_id.xy >= resolution))
    {
        return;
    }

    const float denoising_range = max(buffer_frame.camera_far * 0.99f, 1.0f);

    // nrd reads the checkerboard signal from the left half only, the right half is left untouched
    bool checkerboard = pass_bool(pass_nrd_reflections::checkerboard);
    if (!checkerboard || thread_id.x < (resolution.x + 1) / 2)
    {
        uint2 pixel = checkerboard ? reflections_checkerboard_pixel(thread_id.xy) : thread_id.xy;
        tex_uav4[thread_id.xy] = pack_radiance(thread_id.xy, pixel, resolution, denoising_range);
    }

    const float2 uv = (thread_id.xy + 0.5f) / float2(resolution);
    float depth  = get_depth(uv);
    float view_z = abs(get_position_view_space(uv).z);
    // a pixel past the roughness cut carries no traced signal, nrd skips it the way it skips the sky
    if (depth <= 0.0f || view_z >= denoising_range || get_rt_reflection_cached(uv) >= 1.0f)
    {
        tex_uav[thread_id.xy]  = float4(0.0f, 0.0f, 0.0f, 0.0f);
        tex_uav2[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        tex_uav3[thread_id.xy] = float4(denoising_range * 2.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 normal_ws    = get_normal(uv);
    float2 velocity_ndc = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).xy;
    float2 mv           = velocity_ndc * float2(-0.5f, 0.5f);

    tex_uav[thread_id.xy]  = float4(mv, 0.0f, 0.0f);
    tex_uav2[thread_id.xy] = NRD_FrontEnd_PackNormalAndRoughness(normal_ws, get_nrd_roughness(uv), 0.0f);
    tex_uav3[thread_id.xy] = float4(view_z, 0.0f, 0.0f, 0.0f);
}
