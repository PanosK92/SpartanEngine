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
    float4 diffuse = tex4[tid.xy];
    float3 raw = tex[tid.xy].rgb;
    float depth = tex2[tid.xy].r;
    float3 color = raw;
    if (depth < max(buffer_frame.camera_far * 0.99f, 1.0f))
    {
        float3 filtered = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(tex3[tid.xy]).rgb;
        // On a dark floor one diffuse path can hit a bright emitter. REBLUR spreads
        // that sample over its kernel. Bound the result by nearby accumulated
        // diffuse colors, excluding isolated extrema and samples across depth edges.
        float3 lowest = asfloat(0x7f7fffffu), second_lowest = lowest;
        float3 highest = 0.0f, second_highest = 0.0f;
        uint neighbours = 0;
        float3 glossy = max(raw - diffuse.rgb, 0.0f);
        float3 glossy_sum = 0.0f;
        float glossy_weight = 0.0f;
        for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            int2 p = int2(tid.xy) + int2(x, y);
            if (any(p < 0) || any(p >= int2(size))) continue;
            if (abs(tex2[p].r - depth) > max(0.01f * depth, 0.001f)) continue;
            float3 c = tex4[p].rgb;
            // A narrow joint bilateral filter on the residual calms fine grain on polished
            // floors. Per-channel range weights stop red or white tubes bleeding into dark pixels.
            float3 reflected = max(tex[p].rgb - c, 0.0f);
            float3 difference = (reflected - glossy) / max(max(glossy, reflected), 1e-4f);
            float w = exp2(-16.0f * dot(difference, difference)) * ((x == 0) ? 2.0f : 1.0f) * ((y == 0) ? 2.0f : 1.0f);
            glossy_sum += w * reflected;
            glossy_weight += w;
            second_lowest = min(second_lowest, max(lowest, c));
            second_highest = max(second_highest, min(highest, c));
            lowest = min(lowest, c);
            highest = max(highest, c);
            ++neighbours;
        }
        filtered = neighbours >= 4 ? clamp(filtered, second_lowest, second_highest) : diffuse.rgb;
        // Keep filtering uncertain pixels, including isolated saturated paths. Restore raw
        // detail where the RGB mean has a small standard error relative to the signal.
        uint samples = pass_uint(pass_path_tracing::samples);
        float relative_error_sq = max(diffuse.a, 0.0f) /
            (max(float(samples) - 1.0f, 1.0f) * max(dot(diffuse.rgb, diffuse.rgb), 1e-8f));
        float weight = samples < 8 ? 1.0f : smoothstep(0.0001f, 0.0025f, relative_error_sq);
        if (all(isfinite(filtered)))
            color = lerp(glossy, glossy_sum / max(glossy_weight, 1e-8f), rsqrt(1.0f + samples / 16.0f))
                  + lerp(diffuse.rgb, max(filtered, 0.0f), weight);
    }
    tex_uav[tid.xy] = float4(color, 1.0f);
}
