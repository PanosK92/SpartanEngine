/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "common.hlsl"
#include "nrd/NRD.hlsli"

[numthreads(8, 8, 1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint2 size;
    tex_uav.GetDimensions(size.x, size.y);
    if (any(tid.xy >= size)) return;
    float3 raw = tex[tid.xy].rgb;
    float depth = tex2[tid.xy].r;
    float3 color = raw;
    if (depth < max(buffer_frame.camera_far * 0.99f, 1.0f))
    {
        float3 filtered = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(tex3[tid.xy]).rgb * tex4[tid.xy].rgb;
        // Spatial smoothing helps early samples most. Let converged, high-frequency detail return.
        float weight = rsqrt(1.0f + float(pass_uint(pass_path_tracing::samples)) / 12.0f);
        if (all(isfinite(filtered)))
            color = lerp(raw, max(filtered, 0.0f), weight);
    }
    tex_uav[tid.xy] = float4(color, 1.0f);
}
