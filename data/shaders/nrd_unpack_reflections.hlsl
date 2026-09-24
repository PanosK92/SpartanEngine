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

// unpacks nrd reblur specular into reflections
// tex     = out_spec_radiance_hitdist
// tex_uav = reflections

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    if (any(thread_id.xy >= resolution))
    {
        return;
    }

    float3 radiance = max(REBLUR_BackEnd_UnpackRadianceAndNormHitDist(tex[thread_id.xy]).xyz, 0.0f);
    tex_uav[thread_id.xy] = float4(radiance, 1.0f);
}
