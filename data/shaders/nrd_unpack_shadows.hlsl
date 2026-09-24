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

// unpacks sigma shadow into ray_traced_shadows.r or a local array slice
// pass_f3_value.y = local array slice
// pass_f3_value.z = 0 sun, 1 local light
// tex     = out_shadow_translucency
// tex_uav = ray_traced_shadows

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
    const uint  local_slice = (uint)pass_get_f3_value().y;
    const bool  is_local    = pass_get_f3_value().z > 0.5f;
    if (depth <= 0.0f)
    {
        if (is_local)
        {
            tex_uav_rt_shadows_local[uint3(thread_id.xy, local_slice)] = float4(1.0f, 0.0f, 0.0f, 1.0f);
        }
        else
        {
            tex_uav[thread_id.xy] = float4(1.0f, 0.0f, 0.0f, 1.0f);
        }
        return;
    }

    float visibility = saturate(SIGMA_BackEnd_UnpackShadow(tex[thread_id.xy]).x);
    if (is_local)
    {
        tex_uav_rt_shadows_local[uint3(thread_id.xy, local_slice)] = float4(visibility, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        tex_uav[thread_id.xy] = float4(visibility, 0.0f, 0.0f, 1.0f);
    }
}
