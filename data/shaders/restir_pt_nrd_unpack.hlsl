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
