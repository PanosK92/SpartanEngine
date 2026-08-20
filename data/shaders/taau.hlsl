/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions:

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

static const float blend_static       = 1.0f / 24.0f;
static const float blend_motion       = 1.0f / 4.0f;
static const float blend_flicker_min  = 0.3f;
static const float motion_px_full     = 24.0f;
static const float box_widen_static   = 0.5f;
static const float box_pad_relative   = 0.08f;
static const float box_pad_absolute   = 0.002f;
static const float box_pad_hdr        = 0.2f;
// relative depth tolerance for same surface
static const float reuse_depth_tol = 0.02f;

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

float max3(float3 c)
{
    return max(c.r, max(c.g, c.b));
}

float3 sample_history(float2 uv, float2 res)
{
    float2 sample_pos = uv * res;
    float2 tc1        = floor(sample_pos - 0.5f) + 0.5f;
    float2 f          = sample_pos - tc1;
    float2 f2         = f * f;
    float2 f3         = f2 * f;

    float2 w0 = f2 - 0.5f * (f3 + f);
    float2 w1 = 1.5f * f3 - 2.5f * f2 + 1.0f;
    float2 w3 = 0.5f * (f3 - f2);
    float2 w2 = 1.0f - w0 - w1 - w3;

    float2 w12  = max(w1 + w2, 1e-5f);
    float2 tc0  = (tc1 - 1.0f) / res;
    float2 tc3  = (tc1 + 2.0f) / res;
    float2 tc12 = (tc1 + w2 / w12) / res;

    float3 s0 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tc12.x, tc0.y),  0.0f).rgb;
    float3 s1 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tc0.x,  tc12.y), 0.0f).rgb;
    float3 s2 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tc12.x, tc12.y), 0.0f).rgb;
    float3 s3 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tc3.x,  tc12.y), 0.0f).rgb;
    float3 s4 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tc12.x, tc3.y),  0.0f).rgb;

    float k0 = w12.x * w0.y;
    float k1 = w0.x  * w12.y;
    float k2 = w12.x * w12.y;
    float k3 = w3.x  * w12.y;
    float k4 = w12.x * w3.y;

    float  k_sum  = k0 + k1 + k2 + k3 + k4;
    float3 result = s0 * k0 + s1 * k1 + s2 * k2 + s3 * k3 + s4 * k4;
    result        = result * rcp(max(k_sum, 1e-5f));

    if (any(isnan(result)) || any(isinf(result)))
    {
        result = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0.0f).rgb;
    }

    return max(result, 0.0f.xxx);
}

float3 clip_to_aabb(float3 box_min, float3 box_max, float3 history)
{
    float3 center = 0.5f * (box_max + box_min);
    float3 extent = max(0.5f * (box_max - box_min), 1e-5f);
    float3 offset = history - center;
    float3 units  = abs(offset) * rcp(extent);
    float  ratio  = max3(units);

    return (ratio > 1.0f) ? (center + offset * rcp(ratio)) : history;
}

// sky has no gbuffer velocity, rebuild camera rotation at infinity
float2 compute_sky_velocity(float2 uv)
{
    matrix vp_curr = pass_is_right_eye() ?
        buffer_frame.view_projection_unjittered_right :
        buffer_frame.view_projection_unjittered;
    matrix vp_prev = pass_is_right_eye() ?
        buffer_frame.view_projection_previous_unjittered_right :
        buffer_frame.view_projection_previous_unjittered;

    float2 ndc      = uv_to_ndc(uv);
    float4 world    = mul(float4(ndc, 0.0001f, 1.0f), get_view_projection_inverted());
    float3 view_dir = normalize(world.xyz / world.w - get_camera_position());

    static const float sky_distance = 10000.0f;
    float3 sky_curr = get_camera_position()                 + view_dir * sky_distance;
    float3 sky_prev = buffer_frame.camera_position_previous + view_dir * sky_distance;

    float4 curr_clip = mul(float4(sky_curr, 1.0f), vp_curr);
    float4 prev_clip = mul(float4(sky_prev, 1.0f), vp_prev);
    return curr_clip.xy / max(curr_clip.w, 1e-6f) - prev_clip.xy / max(prev_clip.w, 1e-6f);
}

// compare expected previous depth of this surface against what was stored
float compute_history_reuse(int2 px_render, float2 res_render, int2 px_render_max, float2 velocity_ndc)
{
    float  depth_raw = tex_depth[px_render].r;
    float2 uv_render = (float2(px_render) + 0.5f) / res_render;
    float3 position  = get_position(depth_raw, uv_render);
    float4 prev_clip = mul(float4(position, 1.0f), get_view_projection_previous());
    if (prev_clip.w < 1e-6f)
    {
        return 0.0f;
    }

    float2 prev_uv = ndc_to_uv(prev_clip.xy / prev_clip.w);
    if (any(prev_uv < 0.0f) || any(prev_uv > 1.0f))
    {
        return 0.0f;
    }

    int2  prev_px        = clamp(int2(prev_uv * res_render), int2(0, 0), px_render_max);
    float prev_depth_raw = tex3[prev_px].r;
    float expected       = linearize_depth(prev_clip.z / prev_clip.w);
    float actual         = linearize_depth(prev_depth_raw);
    float abs_delta      = abs(actual - expected);
    if (abs_delta <= reuse_depth_tol * max(expected, 1e-3f))
    {
        return 1.0f;
    }

    // mismatch, keep only if a still neighbour owns that previous depth
    static const float still_px = 0.5f;
    static const int2 n4[4] =
    {
        int2(1, 0), int2(-1, 0),
        int2(0, 1), int2(0, -1)
    };
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        int2 tap = px_render + n4[i];
        if (any(tap < 0) || any(tap > px_render_max))
        {
            continue;
        }

        float2 vel_n  = tex_velocity[tap].xy;
        float  rel_px = length((vel_n - velocity_ndc) * float2(0.5f, -0.5f) * res_render);
        if (rel_px > still_px)
        {
            continue;
        }

        float z_n = linearize_depth(tex_depth[tap].r);
        if (abs(actual - z_n) <= reuse_depth_tol * max(z_n, 1e-3f))
        {
            return 1.0f;
        }
    }

    return 0.0f;
}

float3 taau(uint2 px_out, float2 res_out)
{
    float2 uv_out        = (px_out + 0.5f) / res_out;
    uint2  active_render = uint2(get_render_resolution_active());
    int2   px_render_max = max(int2(active_render) - 1, int2(0, 0));

    float2 active_render_f = float2(active_render);
    float2 jitter_px       = buffer_frame.taa_jitter_current * float2(0.5f, -0.5f) * active_render_f;
    float2 p_render        = uv_out * active_render_f + jitter_px;

    p_render      = clamp(p_render, float2(0.5f, 0.5f), active_render_f - 0.5f);
    int2 center   = clamp(int2(floor(p_render)), int2(0, 0), px_render_max);

    float2 d_base = (float2(center) + 0.5f) - p_render;
    float3 wx     = float3(
        reconstruct_weight((d_base.x - 1.0f) * (d_base.x - 1.0f)),
        reconstruct_weight(d_base.x * d_base.x),
        reconstruct_weight((d_base.x + 1.0f) * (d_base.x + 1.0f)));
    float3 wy     = float3(
        reconstruct_weight((d_base.y - 1.0f) * (d_base.y - 1.0f)),
        reconstruct_weight(d_base.y * d_base.y),
        reconstruct_weight((d_base.y + 1.0f) * (d_base.y + 1.0f)));

    float3 rgb_min_near   =  FLT_MAX_16U.xxx;
    float3 rgb_max_near   = -FLT_MAX_16U.xxx;
    float3 rgb_min_wide   =  FLT_MAX_16U.xxx;
    float3 rgb_max_wide   = -FLT_MAX_16U.xxx;
    float3 current_rgb_tm = 0.0f.xxx;
    float  weight_sum     = 0.0f;

    int2  closest_px    = center;
    float closest_depth = -1.0f;

    // corners widen the near aabb built from the 3x3
    static const int2 wide_corners[4] =
    {
        int2(-2, -2), int2( 2, -2),
        int2(-2,  2), int2( 2,  2)
    };

    [unroll]
    for (int oy = -1; oy <= 1; ++oy)
    {
        [unroll]
        for (int ox = -1; ox <= 1; ++ox)
        {
            int2 tap = center + int2(ox, oy);
            if (any(tap < 0) || any(tap > px_render_max))
            {
                continue;
            }

            float3 s_rgb_raw = tex2[tap].rgb;
            if (any(isnan(s_rgb_raw)) || any(isinf(s_rgb_raw)))
            {
                continue;
            }

            float3 s_tm = tonemap_for_taa(clamp(s_rgb_raw, 0.0f.xxx, FLT_MAX_16U.xxx));
            rgb_min_near = min(rgb_min_near, s_tm);
            rgb_max_near = max(rgb_max_near, s_tm);
            rgb_min_wide = min(rgb_min_wide, s_tm);
            rgb_max_wide = max(rgb_max_wide, s_tm);

            float w         = wx[ox + 1] * wy[oy + 1];
            current_rgb_tm += s_tm * w;
            weight_sum     += w;

            float d = tex_depth[tap].r;
            if (d > closest_depth)
            {
                closest_depth = d;
                closest_px    = tap;
            }
        }
    }

    [unroll]
    for (int c = 0; c < 4; ++c)
    {
        int2 tap = center + wide_corners[c];
        if (any(tap < 0) || any(tap > px_render_max))
        {
            continue;
        }

        float3 s_rgb_raw = tex2[tap].rgb;
        if (any(isnan(s_rgb_raw)) || any(isinf(s_rgb_raw)))
        {
            continue;
        }

        float3 s_tm = tonemap_for_taa(clamp(s_rgb_raw, 0.0f.xxx, FLT_MAX_16U.xxx));
        rgb_min_wide = min(rgb_min_wide, s_tm);
        rgb_max_wide = max(rgb_max_wide, s_tm);
    }

    bool current_valid = weight_sum > 0.0f;
    current_rgb_tm     = current_valid ? current_rgb_tm * rcp(weight_sum) : 0.0f.xxx;

    // dilated velocity is for the history uv only, the reuse test uses this pixel
    float  center_depth    = tex_depth[center].r;
    bool   is_sky          = center_depth < 1e-4f;
    float2 velocity_center = tex_velocity[center].xy;
    if (is_sky && dot(velocity_center, velocity_center) < 1e-12f)
    {
        velocity_center = compute_sky_velocity(uv_out);
    }
    float2 velocity_ndc = is_sky ? velocity_center : tex_velocity[closest_px].xy;
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    float2 uv_prev      = uv_out - velocity_uv;

    float2 inset           = 1.5f / res_out;
    bool   uv_prev_valid   = all(uv_prev > inset) && all(uv_prev < 1.0f - inset);
    bool   history_invalid = reset_history() > 0.5f || !uv_prev_valid;

    if (!current_valid)
    {
        float3 fallback = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_out, 0.0f).rgb;
        return saturate_16(max(fallback, 0.0f.xxx));
    }

    if (history_invalid)
    {
        return saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx));
    }

    float motion_px = length(velocity_uv * res_out);
    float motion    = saturate(motion_px * rcp(motion_px_full));

    // never gate on motion, a revealed sky pixel has no velocity
    float reuse = compute_history_reuse(center, active_render_f, px_render_max, velocity_center);
    if (reuse < 1.0f)
    {
        return saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx));
    }

    float3 history_rgb = sample_history(uv_prev, res_out);
    if (any(isnan(history_rgb)) || any(isinf(history_rgb)))
    {
        return saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx));
    }

    float  widen   = box_widen_static * (1.0f - motion);
    float3 rgb_min = lerp(rgb_min_near, rgb_min_wide, widen);
    float3 rgb_max = lerp(rgb_max_near, rgb_max_wide, widen);

    // reinhard compresses highlights, restore a constant relative tolerance for bright taps
    float  box_level = max3(rgb_max);
    float  pad_hdr   = box_pad_hdr * box_level * (1.0f - box_level);
    float3 box_pad   = max((rgb_max - rgb_min) * box_pad_relative, max(box_pad_absolute, pad_hdr));
    rgb_min         -= box_pad;
    rgb_max         += box_pad;

    float3 history_rgb_tm     = tonemap_for_taa(max(history_rgb, 0.0f.xxx));
    float3 history_clipped_tm = clip_to_aabb(rgb_min, rgb_max, history_rgb_tm);

    float blend_base = lerp(blend_static, blend_motion, motion);

    // measure in linear, a small wobble at the top of the curve is a large brightness swing
    float curr_l    = max3(tonemap_for_taa_inv(current_rgb_tm));
    float hist_l    = max3(tonemap_for_taa_inv(history_clipped_tm));
    float disagree  = abs(curr_l - hist_l) * rcp(max(max(curr_l, hist_l), 0.1f));
    float stability = 1.0f - saturate(disagree);
    stability       = stability * stability;

    float blend_flicker = lerp(blend_base * blend_flicker_min, blend_base, stability);
    float blend         = lerp(blend_flicker, blend_base, motion);

    float3 result_rgb_tm = max(lerp(history_clipped_tm, current_rgb_tm, blend), 0.0f.xxx);

    float l_tm      = max3(result_rgb_tm);
    float l_tm_safe = min(l_tm, 1.0f - 1e-3f);
    result_rgb_tm  *= (l_tm > 0.0f) ? (l_tm_safe / l_tm) : 1.0f;
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
