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

// sky has no gbuffer velocity, rebuild camera rotation at infinity so history
// tracks the dome instead of smearing into curved ghost trails
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

    // load 3x3, drop non finite taps
    float3 taps[9];
    bool   tap_valid[9];
    float  tap_lum[9];
    float  neigh_max_l = 0.0f;

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        int2 tap = center + int2((i % 3) - 1, (i / 3) - 1);
        tap_valid[i] = all(tap >= 0) && all(tap <= px_render_max);
        taps[i]      = 0.0f.xxx;
        tap_lum[i]   = 0.0f;
        if (!tap_valid[i])
        {
            continue;
        }

        float3 s_rgb_raw = tex2[tap].rgb;
        if (any(isnan(s_rgb_raw)) || any(isinf(s_rgb_raw)))
        {
            tap_valid[i] = false;
            continue;
        }

        taps[i]    = tonemap_for_taa(clamp(s_rgb_raw, 0.0f.xxx, FLT_MAX_16U.xxx));
        tap_lum[i] = max3(taps[i]);
        neigh_max_l = max(neigh_max_l, tap_lum[i]);
    }

    // reconstruct current, ignore outlier dark taps (specular holes, gap bleed)
    float3 rgb_min        =  FLT_MAX_16U.xxx;
    float3 rgb_max        = -FLT_MAX_16U.xxx;
    float3 current_rgb_tm = 0.0f.xxx;
    float  weight_sum     = 0.0f;
    float  dark_cut       = neigh_max_l * 0.15f;

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        if (!tap_valid[i])
        {
            continue;
        }

        // keep at least the center tap even if the whole neighbourhood is dark
        bool keep = (i == 4) || (tap_lum[i] >= dark_cut);
        if (!keep)
        {
            continue;
        }

        rgb_min = min(rgb_min, taps[i]);
        rgb_max = max(rgb_max, taps[i]);

        int dx = (i % 3) - 1;
        int dy = (i / 3) - 1;
        float w = wx[dx + 1] * wy[dy + 1];
        current_rgb_tm += taps[i] * w;
        weight_sum     += w;
    }

    bool current_valid = weight_sum > 0.0f;
    current_rgb_tm     = current_valid ? current_rgb_tm * rcp(weight_sum) : 0.0f.xxx;

    // center velocity, closest depth dilation pulls panel gap motion into paint
    float  center_depth = tex_depth[center].r;
    bool   is_sky       = center_depth < 1e-4f;
    float2 velocity_ndc = tex_velocity[center].xy;
    if (is_sky && dot(velocity_ndc, velocity_ndc) < 1e-12f)
    {
        velocity_ndc = compute_sky_velocity(uv_out);
    }
    float2 velocity_uv = velocity_ndc * float2(0.5f, -0.5f);
    float2 uv_prev     = uv_out - velocity_uv;

    float2 inset           = 1.5f / res_out;
    bool   uv_prev_valid   = all(uv_prev > inset) && all(uv_prev < 1.0f - inset);
    bool   history_invalid = reset_history() > 0.5f || !uv_prev_valid;

    if (!current_valid)
    {
        // no current signal, try history at this uv
        float3 fallback = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_out, 0.0f).rgb;
        return saturate_16(max(fallback, 0.0f.xxx));
    }

    if (history_invalid)
    {
        return saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx));
    }

    float3 history_rgb = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_prev, 0.0f).rgb;
    if (any(isnan(history_rgb)) || any(isinf(history_rgb)))
    {
        return saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx));
    }

    float3 history_rgb_tm     = tonemap_for_taa(max(history_rgb, 0.0f.xxx));
    float3 history_clipped_tm = clamp(history_rgb_tm, rgb_min, rgb_max);

    float curr_l = max3(current_rgb_tm);
    float hist_l = max3(history_clipped_tm);
    float motion = saturate(length(velocity_uv * res_out) * (1.0f / 64.0f));
    float blend  = lerp(1.0f / 8.0f, 1.0f / 4.0f, motion);

    if (curr_l + 1e-4f < hist_l)
    {
        // current darker, specular hole, keep history
        blend *= saturate(curr_l * rcp(max(hist_l, 1e-3f)));
    }
    else if (hist_l + 1e-4f < curr_l)
    {
        // history darker, dump poisoned black so it cannot linger at blend 1/8
        blend = max(blend, saturate(1.0f - hist_l * rcp(max(curr_l, 1e-3f))));
    }

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
