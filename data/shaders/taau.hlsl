#include "common.hlsl"

float reset_history() { return pass_get_f3_value().x; }

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

// history sampling
float3 sample_history(float2 uv)
{
    return max(tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0.0f).rgb, 0.0f.xxx);
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
    float3 cmin           =  FLT_MAX_16U.xxx;
    float3 cmax           = -FLT_MAX_16U.xxx;
    float3 current_rgb_tm = 0.0f.xxx;
    float  weight_sum     = 0.0f;
    float  count          = 0.0f;

    float2 d_base = (float2(center) + 0.5f) - p_render;
    float3 wx     = float3(reconstruct_weight((d_base.x - 1.0f) * (d_base.x - 1.0f)), reconstruct_weight(d_base.x * d_base.x), reconstruct_weight((d_base.x + 1.0f) * (d_base.x + 1.0f)));
    float3 wy     = float3(reconstruct_weight((d_base.y - 1.0f) * (d_base.y - 1.0f)), reconstruct_weight(d_base.y * d_base.y), reconstruct_weight((d_base.y + 1.0f) * (d_base.y + 1.0f)));

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

            float3 s_rgb_raw  = tex2[tap].rgb;
            float3 s_rgb      = isnan(s_rgb_raw.x + s_rgb_raw.y + s_rgb_raw.z) ? 0.0f.xxx : max(s_rgb_raw, 0.0f.xxx);
            float3 s_rgb_tm   = tonemap_for_taa(s_rgb);

            m1    += s_rgb_tm;
            cmin   = min(cmin, s_rgb_tm);
            cmax   = max(cmax, s_rgb_tm);
            count += 1.0f;

            float  w = wx[dx + 1] * wy[dy + 1];
            current_rgb_tm += s_rgb_tm * w;
            weight_sum     += w;
        }
    }

    current_rgb_tm          = current_rgb_tm * rcp(weight_sum);

    // history reprojection
    float2 velocity_ndc = tex_velocity[center].xy;
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    float2 uv_prev      = uv_out - velocity_uv;

    float2 inset           = 1.5f / res_out;
    bool   uv_prev_valid   = all(uv_prev > inset) && all(uv_prev < 1.0f - inset);
    bool   history_invalid = reset_history() > 0.5f || !uv_prev_valid;
    if (history_invalid)
    {
        return saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx));
    }

    float3 history_rgb      = sample_history(uv_prev);
    float3 history_rgb_tm   = tonemap_for_taa(history_rgb);

    // history clipping
    float  inv_count = rcp(count);
    float3 mean      = m1 * inv_count;
    float  motion    = saturate(length(velocity_uv * res_out) * (1.0f / 64.0f));
    float  contrast  = saturate(max(max(cmax.r - cmin.r, cmax.g - cmin.g), cmax.b - cmin.b) * 4.0f);
    float  clip_scale = lerp(0.75f, 0.35f, max(motion, contrast));
    float3 clip_extent = max((cmax - cmin) * clip_scale, 0.005f.xxx);
    float3 aabb_min  = max(mean - clip_extent, cmin);
    float3 aabb_max  = min(mean + clip_extent, cmax);
    float3 history_c = clamp(history_rgb_tm, aabb_min, aabb_max);

    // accumulation
    float blend = lerp(1.0f / 8.0f, 1.0f / 4.0f, max(motion, contrast));

    float3 result_rgb_tm = max(lerp(history_c, current_rgb_tm, blend), 0.0f.xxx);

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
