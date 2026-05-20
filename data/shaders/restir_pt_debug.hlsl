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

//= INCLUDES ===================
#include "common.hlsl"
#include "restir_reservoir.hlsl"
//==============================

// debug visualization for restir state
// reads the denoised gi (rgb = color, alpha = variance from the svgf temporal pass), the previous frame reservoirs (M, W, confidence), and produces a viridis heatmap of the requested quantity
// the result is written to tex_uav in place of the gi color so the composite remultiplies by albedo as usual, producing a tinted but readable overlay
//
// debug_mode values match the r.restir_pt_debug_mode cvar list in RenderOptions:
//   1 = confidence (low f16 of tex3.z, mapped 0..1)
//   2 = reservoir M (0..RESTIR_M_CAP)
//   3 = reservoir W (log scaled since W has high dynamic range)
//   4 = reuse ratio M/m_cap, saturation against the runtime cvar cap, identifies where temporal accumulation is healthy vs freshly reset
//   5 = path length, the actual chosen sample's bounce count (1..max_path_length), highlights where paths are short and indirect contribution is missing
//   6 = variance (alpha of the denoised gi, log scaled, the svgf per pixel luminance variance)
//   7 = path flags shift type, encodes the chosen sample's strategy and rc validity:
//       blue   = sky / sun cone nee (PATH_FLAG_SKY)
//       green  = surface rc with reconnection enabled (PATH_FLAG_HAS_RC and not nee)
//       red    = nee area light (PATH_FLAG_HAS_RC and PATH_FLAG_NEE)
//       yellow = no rc, replay-only path
//   8 = target_pdf / W stability, log scaled, low values indicate a stable estimator (target ~ W * weight_sum), high values indicate unstable single-sample dominance, useful to spot pixels where the m cap is too low or the sample is degenerate

// viridis colormap, approximated via a small polynomial fit, returns a perceptually uniform color from a [0,1] input
float3 viridis(float t)
{
    t = saturate(t);
    float3 c0 = float3(0.2777f, 0.0054f, 0.3340f);
    float3 c1 = float3(0.1056f, 1.4046f, 1.3845f);
    float3 c2 = float3(-0.3308f, 0.2148f, 0.0950f);
    float3 c3 = float3(-4.6342f, -5.7991f, -19.3324f);
    float3 c4 = float3(6.2289f, 14.1799f, 56.6905f);
    float3 c5 = float3(4.7763f, -13.7451f, -65.3530f);
    float3 c6 = float3(-5.4354f, 4.6458f, 26.3124f);
    return saturate(c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6))))));
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    uint resolution_x, resolution_y;
    tex_uav.GetDimensions(resolution_x, resolution_y);
    uint2 resolution = uint2(resolution_x, resolution_y);

    if (pixel.x >= resolution.x || pixel.y >= resolution.y)
    {
        return;
    }

    uint mode = uint(buffer_frame.restir_pt_debug_mode);
    if (mode == 0)
    {
        return;
    }

    // sample what we need based on the mode
    // reservoir packing matches pack_reservoir / unpack_reservoir in restir_reservoir.hlsl
    float visualization_t = 0.0f;
    [branch] switch (mode)
    {
        case 1: // confidence (high f16 of tex3.z, paired with rc_roughness in the low f16)
        {
            uint  packed     = asuint(tex_reservoir_prev3[pixel].z);
            float confidence = saturate(f16tof32(packed >> 16u));
            visualization_t  = confidence;
            break;
        }
        case 2: // reservoir M, normalized by RESTIR_M_CAP for [0,1]
        {
            float M = tex_reservoir_prev2[pixel].w;
            visualization_t = saturate(M / float(RESTIR_M_CAP));
            break;
        }
        case 3: // reservoir W, log scaled since W spans many decades
        {
            float W = tex_reservoir_prev3[pixel].x;
            visualization_t = saturate(log2(W + 1.0f) / 8.0f);
            break;
        }
        case 4: // reuse ratio M / runtime m_cap, hot pixels are saturated and benefit fully from temporal accumulation, cold pixels were reset by disocclusion / validation
        {
            float M       = tex_reservoir_prev2[pixel].w;
            float m_cap   = max(get_restir_m_cap(), 1.0f);
            visualization_t = saturate(M / m_cap);
            break;
        }
        case 5: // path length, decoded from the packed path_info word so we can see whether paths actually reach max bounces
        {
            uint  packed_info = asuint(tex_reservoir_prev2[pixel].y);
            uint  path_length;
            uint  rc_length;
            uint  flags;
            unpack_path_info(packed_info, path_length, rc_length, flags);
            float max_path  = max(float(get_restir_max_path_length()), 1.0f);
            visualization_t = saturate(float(path_length) / max_path);
            break;
        }
        case 6: // variance (alpha of the denoised gi, log scaled because per pixel variance spans several decades on disocclusion edges)
        {
            float variance = max(tex_uav[pixel].a, 0.0f);
            visualization_t = saturate(log2(variance + 1.0f) / 4.0f);
            break;
        }
        case 7: // path flags shift category, distinct color per strategy so the reconnection/nee/replay split is readable at a glance
        {
            uint  packed_info = asuint(tex_reservoir_prev2[pixel].y);
            uint  path_length;
            uint  rc_length;
            uint  flags;
            unpack_path_info(packed_info, path_length, rc_length, flags);

            float3 color = float3(1.0f, 1.0f, 0.0f); // yellow: no rc / replay-only
            const uint flag_sky    = 1u << 0;
            const uint flag_has_rc = 1u << 1;
            const uint flag_nee    = 1u << 3;
            if ((flags & flag_sky) != 0u)
            {
                color = float3(0.2f, 0.4f, 1.0f); // blue: sky/sun cone nee
            }
            else if ((flags & flag_has_rc) != 0u && (flags & flag_nee) != 0u)
            {
                color = float3(1.0f, 0.2f, 0.2f); // red: area light nee
            }
            else if ((flags & flag_has_rc) != 0u)
            {
                color = float3(0.2f, 1.0f, 0.4f); // green: surface rc reconnection
            }
            float alpha_in = tex_uav[pixel].a;
            tex_uav[pixel] = float4(color, alpha_in);
            return;
        }
        case 8: // target_pdf / W stability, low ratio means the estimator is well distributed across many samples, high ratio means a single sample dominates
        {
            float W           = tex_reservoir_prev3[pixel].x;
            float target_pdf  = tex_reservoir_prev3[pixel].y;
            float ratio       = target_pdf / max(W, 1e-6f);
            visualization_t   = saturate(log2(ratio + 1.0f) / 8.0f);
            break;
        }
    }

    // composition adds light_gi directly when restir is enabled, no albedo remultiply is done
    // so we write the viridis color straight into the gi slot, the confidence stored in alpha
    // is preserved so downstream bilateral upsampling still has a sane weight
    float3 heatmap   = viridis(visualization_t);
    float  alpha_in  = tex_uav[pixel].a;
    tex_uav[pixel]   = float4(heatmap, alpha_in);
}
