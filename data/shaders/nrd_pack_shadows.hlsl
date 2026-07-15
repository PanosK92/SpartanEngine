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

// packs guides + rt shadow (visibility, blocker_dist) into sigma penumbra
// pass_f3_value.x = tan(light_angular_radius)
// tex       = ray_traced_shadows
// tex_uav   = in_mv
// tex_uav2  = in_normal_roughness
// tex_uav3  = in_viewz
// tex_uav4  = in_penumbra (r32f)

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
    float view_z = abs(get_position_view_space(uv).z);
    const float denoising_range = max(buffer_frame.camera_far * 0.99f, 1.0f);
    const float tan_light_angular_radius = pass_get_f3_value().x;

    if (depth <= 0.0f || view_z >= denoising_range)
    {
        tex_uav[thread_id.xy]  = 0.0f;
        tex_uav2[thread_id.xy] = 0.0f;
        tex_uav3[thread_id.xy] = float4(denoising_range * 2.0f, 0.0f, 0.0f, 0.0f);
        tex_uav4[thread_id.xy] = float4(NRD_FP16_MAX, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 normal_ws = get_normal(uv);
    float2 velocity_ndc = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).xy;
    float2 mv = velocity_ndc * float2(-0.5f, 0.5f);

    float4 shadow = tex[thread_id.xy];
    float visibility   = shadow.r;
    float blocker_dist = shadow.g;

    // lit or no blocker, sigma wants a large distance
    float distance_to_occluder = (visibility >= 0.99f || blocker_dist <= 0.0f) ? NRD_FP16_MAX : blocker_dist;
    float penumbra = SIGMA_FrontEnd_PackPenumbra(distance_to_occluder, tan_light_angular_radius);

    tex_uav[thread_id.xy]  = float4(mv, 0.0f, 0.0f);
    tex_uav2[thread_id.xy] = NRD_FrontEnd_PackNormalAndRoughness(normal_ws, 1.0f, 0.0f);
    tex_uav3[thread_id.xy] = float4(view_z, 0.0f, 0.0f, 0.0f);
    tex_uav4[thread_id.xy] = float4(penumbra, 0.0f, 0.0f, 0.0f);
}
