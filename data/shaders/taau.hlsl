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

#include "common.hlsl"

float reset_history() { return pass_get_f3_value().x; }

// color space
float3 rgb_to_ycocg(float3 c)
{
    float y  = dot(c, float3( 0.25f,  0.5f,  0.25f));
    float co = dot(c, float3( 0.5f,   0.0f, -0.5f));
    float cg = dot(c, float3(-0.25f,  0.5f, -0.25f));
    return float3(y, co, cg);
}

float3 ycocg_to_rgb(float3 c)
{
    float y_minus_cg = c.x - c.z;
    return float3(y_minus_cg + c.y, c.x + c.z, y_minus_cg - c.y);
}

float3 tonemap_for_taa(float3 c)
{
    float l = max(c.r, max(c.g, c.b));
    return c * rcp(1.0f + l);
}

float3 tonemap_for_taa_inv(float3 c)
{
    float l = max(c.r, max(c.g, c.b));
    return c * rcp(max(1.0f - l, 1e-3f));
}

float reconstruct_weight(float d_sq)
{
    return exp(-2.29f * d_sq);
}

float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q)
{
    float3 r    = q - p;
    float3 rmax = aabb_max - p;
    float3 rmin = aabb_min - p;

    const float eps = FLT_MIN;
    if (r.x > rmax.x + eps)
    {
        r *= (rmax.x / r.x);
    }
    if (r.y > rmax.y + eps)
    {
        r *= (rmax.y / r.y);
    }
    if (r.z > rmax.z + eps)
    {
        r *= (rmax.z / r.z);
    }
    if (r.x < rmin.x - eps)
    {
        r *= (rmin.x / r.x);
    }
    if (r.y < rmin.y - eps)
    {
        r *= (rmin.y / r.y);
    }
    if (r.z < rmin.z - eps)
    {
        r *= (rmin.z / r.z);
    }

    return p + r;
}

// history sampling
float3 sample_history_catmull_rom(float2 uv, float2 resolution)
{
    float2 sample_position = uv * resolution;
    float2 tex_pos_1       = floor(sample_position - 0.5f) + 0.5f;
    float2 f               = sample_position - tex_pos_1;

    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    float2 w12       = w1 + w2;
    float2 offset_12 = w2 / w12;

    float2 inv_res    = 1.0f / resolution;
    float2 tex_pos_0  = (tex_pos_1 - 1.0f) * inv_res;
    float2 tex_pos_3  = (tex_pos_1 + 2.0f) * inv_res;
    float2 tex_pos_12 = (tex_pos_1 + offset_12) * inv_res;

    float3 h0 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_0.y),  0.0f).rgb;
    float3 h1 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_0.x,  tex_pos_12.y), 0.0f).rgb;
    float3 h2 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_12.y), 0.0f).rgb;
    float3 h3 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_3.x,  tex_pos_12.y), 0.0f).rgb;
    float3 h4 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_3.y),  0.0f).rgb;

    float3 result = 0.0f.xxx;
    result += h0 * w12.x * w0.y;
    result += h1 * w0.x  * w12.y;
    result += h2 * w12.x * w12.y;
    result += h3 * w3.x  * w12.y;
    result += h4 * w12.x * w3.y;

    float  weight_sum  = w12.x + w12.y - w12.x * w12.y;
    float3 history_min = min(min(min(h0, h1), min(h2, h3)), h4);
    float3 history_max = max(max(max(h0, h1), max(h2, h3)), h4);
    return clamp(result * rcp(weight_sum), history_min, history_max);
}

float3 taau(uint2 px_out, float2 res_out)
{
    float2 uv_out        = (px_out + 0.5f) / res_out;
    uint2  active_render = uint2(get_render_resolution_active());
    int2   px_render_max = max(int2(active_render) - 1, int2(0, 0));

    float2 active_render_f = float2(active_render);
    float2 jitter_px       = buffer_frame.taa_jitter_current * float2(0.5f, -0.5f) * active_render_f;
    float2 p_render        = uv_out * active_render_f + jitter_px;

    p_render         = clamp(p_render, float2(0.5f, 0.5f), active_render_f - 0.5f);
    int2   center    = clamp(int2(floor(p_render)), int2(0, 0), px_render_max);

    // current reconstruction
    float3 m1             = 0.0f.xxx;
    float3 m2             = 0.0f.xxx;
    float3 cmin           =  FLT_MAX_16U.xxx;
    float3 cmax           = -FLT_MAX_16U.xxx;
    float3 current_rgb_tm = 0.0f.xxx;
    float  weight_sum     = 0.0f;
    float  count          = 0.0f;

    float2 d_base = (float2(center) + 0.5f) - p_render;
    float3 wx     = float3(reconstruct_weight((d_base.x - 1.0f) * (d_base.x - 1.0f)), reconstruct_weight(d_base.x * d_base.x), reconstruct_weight((d_base.x + 1.0f) * (d_base.x + 1.0f)));
    float3 wy     = float3(reconstruct_weight((d_base.y - 1.0f) * (d_base.y - 1.0f)), reconstruct_weight(d_base.y * d_base.y), reconstruct_weight((d_base.y + 1.0f) * (d_base.y + 1.0f)));

    float closest_depth = tex_depth[center].r;
    int2  closest_pos   = center;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 tap = center + int2(dx, dy);
            if (any(tap < 0) || any(tap > px_render_max))
            {
                continue;
            }

            // nan taps are rejected, inf taps are clamped to fp16 max so bright pixels stay bright
            float3 s_rgb_raw = tex2[tap].rgb;
            if (any(isnan(s_rgb_raw)))
            {
                continue;
            }

            float3 s_rgb      = clamp(s_rgb_raw, 0.0f.xxx, FLT_MAX_16U.xxx);
            float3 s_rgb_tm   = tonemap_for_taa(s_rgb);
            float3 s_ycocg_tm = rgb_to_ycocg(s_rgb_tm);

            m1    += s_ycocg_tm;
            m2    += s_ycocg_tm * s_ycocg_tm;
            cmin   = min(cmin, s_ycocg_tm);
            cmax   = max(cmax, s_ycocg_tm);
            count += 1.0f;

            float  w = wx[dx + 1] * wy[dy + 1];
            current_rgb_tm += s_rgb_tm * w;
            weight_sum     += w;

            if (dx != 0 || dy != 0)
            {
                float z = tex_depth[tap].r;
                if (z > closest_depth)
                {
                    closest_depth = z;
                    closest_pos   = tap;
                }
            }
        }
    }

    bool current_valid      = weight_sum > 0.0f;
    current_rgb_tm          = current_valid ? current_rgb_tm * rcp(weight_sum) : 0.0f.xxx;
    float3 current_ycocg_tm = rgb_to_ycocg(current_rgb_tm);

    // history reprojection
    float2 velocity_ndc = tex_velocity[closest_pos].xy;
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    float2 uv_prev      = uv_out - velocity_uv;

    float2 inset           = 1.5f / res_out;
    bool   uv_prev_valid   = all(uv_prev > inset) && all(uv_prev < 1.0f - inset);
    bool   history_invalid = reset_history() > 0.5f || !uv_prev_valid;
    if (history_invalid)
    {
        return saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx));
    }

    float3 history_rgb = sample_history_catmull_rom(uv_prev, res_out);

    // a stale nan in the history buffer would recirculate forever and spread through the bilinear taps
    if (any(isnan(history_rgb)))
    {
        return saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx));
    }

    // an all nan neighborhood has no current signal, carry the history through unchanged
    if (!current_valid)
    {
        return saturate_16(max(history_rgb, 0.0f.xxx));
    }

    float3 history_rgb_tm   = tonemap_for_taa(history_rgb);
    float3 history_ycocg_tm = rgb_to_ycocg(history_rgb_tm);

    // history clipping
    float  inv_count = rcp(count);
    float3 mean      = m1 * inv_count;
    float3 sigma     = sqrt(max(m2 * inv_count - mean * mean, 0.0f.xxx));
    sigma            = max(sigma, 0.005f);
    float  motion    = saturate(length(velocity_uv * res_out) * (1.0f / 64.0f));
    float  contrast  = saturate((cmax.x - cmin.x) * 4.0f);
    float  gamma     = lerp(1.5f, 1.0f, motion);
    gamma            = lerp(gamma, 0.75f, contrast);
    float3 aabb_min  = max(mean - sigma * gamma, cmin);
    float3 aabb_max  = min(mean + sigma * gamma, cmax);
    float3 history_c = clip_aabb(aabb_min, aabb_max, mean, history_ycocg_tm);

    // accumulation
    float blend = lerp(1.0f / 8.0f, 1.0f / 4.0f, max(motion, contrast));

    float3 result_ycocg_tm = lerp(history_c, current_ycocg_tm, blend);
    float3 result_rgb_tm   = max(ycocg_to_rgb(result_ycocg_tm), 0.0f.xxx);

    float l_tm        = max(result_rgb_tm.r, max(result_rgb_tm.g, result_rgb_tm.b));
    float l_tm_safe   = min(l_tm, 1.0f - 1e-3f);
    result_rgb_tm    *= (l_tm > 0.0f) ? (l_tm_safe / l_tm) : 1.0f;
    float3 result_rgb = result_rgb_tm * rcp(1.0f - l_tm_safe);
    return saturate_16(result_rgb);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(thread_id.xy >= uint2(resolution_out)))
    {
        return;
    }

    tex_uav[thread_id.xy] = float4(taau(thread_id.xy, resolution_out), 1.0f);
}
