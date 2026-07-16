/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ====================
#include "common.hlsl"
#include "nrd/NRD.hlsli"
//===============================

// packs restir gi + gbuffer guides into nrd reblur diffuse inputs
// tex_uav  = in_mv (prev_uv - curr_uv)
// tex_uav2 = in_normal_roughness
// tex_uav3 = in_viewz
// tex_uav4 = in_diff_radiance_hitdist
// tex       = restir_output (demodulated diffuse gi)

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

    float depth = get_depth(uv);
    // must match transposed worldtoview passed to nrd (engine v*m equals nrd m*v after transpose)
    float view_z = abs(get_position_view_space(uv).z);
    const float denoising_range = max(buffer_frame.camera_far * 0.99f, 1.0f);

    // sky, mark invalid for nrd, reverse-z clears far to 0
    if (depth <= 0.0f || view_z >= denoising_range)
    {
        tex_uav[thread_id.xy]  = 0.0f;
        tex_uav2[thread_id.xy] = 0.0f;
        tex_uav3[thread_id.xy] = float4(denoising_range * 2.0f, 0.0f, 0.0f, 0.0f);
        tex_uav4[thread_id.xy] = 0.0f;
        return;
    }

    float3 normal_ws = get_normal(uv);
    float roughness  = max(tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r, 0.04f);

    // engine velocity is unjittered ndc delta (curr - prev), nrd wants uv motion as prev = curr + mv
    float2 velocity_ndc = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).xy;
    float2 mv           = velocity_ndc * float2(-0.5f, 0.5f);

    float3 diff_radiance = max(tex[thread_id.xy].rgb, 0.0f);
    // reconnection distance from the reuse passes, a real hit t lets reblur shrink kernels at contacts
    float hit_dist = max(tex[thread_id.xy].a, 0.0f);
    float3 hit_dist_params = float3(3.0f, 0.1f, 20.0f);
    float norm_hit_dist = REBLUR_FrontEnd_GetNormHitDist(hit_dist, view_z, hit_dist_params, 1.0f);

    tex_uav[thread_id.xy]  = float4(mv, 0.0f, 0.0f);
    tex_uav2[thread_id.xy] = NRD_FrontEnd_PackNormalAndRoughness(normal_ws, roughness, 0.0f);
    tex_uav3[thread_id.xy] = float4(view_z, 0.0f, 0.0f, 0.0f);
    tex_uav4[thread_id.xy] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(diff_radiance, norm_hit_dist, true);
}
