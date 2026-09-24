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

// unpacks nrd reblur diffuse output into restir_denoised (demodulated gi)
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

    float3 radiance = max(REBLUR_BackEnd_UnpackRadianceAndNormHitDist(tex[thread_id.xy]).xyz, 0.0f);
    float count = 1.0f;
    float2 uv = (float2(thread_id.xy) + 0.5f) / float2(resolution);
    if (buffer_pass.values[0].x < 0.5f)
    {
        float4 history = tex2[thread_id.xy];
        float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
        float depth_prev = tex3.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
        float3 normal = get_normal(uv);
        float3 normal_prev = tex5.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).xyz;
        float2 motion = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).xy * buffer_frame.resolution_render * 0.5f;
        float z = linearize_depth(depth);
        bool valid = history.a >= 1.0f && all(isfinite(history)) && depth > 0.0f && depth_prev > 0.0f &&
            abs(linearize_depth(depth_prev) - z) < max(z * 0.01f, 0.001f) &&
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
