/*
Copyright(c) 2016-2026 Panos Karabelas

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

// taau, temporal anti-aliasing with built-in upsampling
// per output pixel:
//   1. reconstruct the current sample at the output pixel center via a sub-pixel
//      mitchell netravali filter over a 3x3 render-res neighborhood that accounts
//      for the projection jitter, this is the actual upsampling step
//   2. fetch reprojected history at output res via a 5-tap catmull-rom
//   3. clip history to a ycocg variance aabb derived from the same 3x3 to suppress ghosting
//   4. depth-reproject the closest surface with the previous unjittered view-projection
//      to estimate disocclusion confidence
//   5. blend with an adaptive feedback factor plus fsr2 style anti-flicker luminance weighting
// bindings, set by the renderer pass:
//   tex          history at output resolution
//   tex2         current jittered color at render resolution, only the top-left
//                resolution_render * resolution_scale region is valid
//   tex_depth    reverse-z depth at render resolution
//   tex_velocity ndc-space velocity at render resolution
//   tex_uav      output at output resolution

//= INCLUDES =========
#include "common.hlsl"
//====================

// values[0].x is a one-shot reset flag, used on the first frame, on resolution
// changes, and on teleports to wipe accumulated history
float reset_history() { return pass_get_f3_value().x; }

/*------------------------------------------------------------------------------
    YCOCG, used for temporal math because chroma decorrelation tightens variance
    bounds and stabilizes accumulation around edges
------------------------------------------------------------------------------*/
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

/*------------------------------------------------------------------------------
    MITCHELL-NETRAVALI B=1/3, C=1/3, the standard cinematic resampling kernel
    used as a separable sub-pixel reconstruction filter for the current sample
------------------------------------------------------------------------------*/
float mitchell(float x)
{
    const float B = 1.0f / 3.0f;
    const float C = 1.0f / 3.0f;
    x          = abs(x);
    float xx   = x * x;
    float xxx  = xx * x;
    if (x < 1.0f)
        return ((12.0f - 9.0f * B - 6.0f * C) * xxx + (-18.0f + 12.0f * B + 6.0f * C) * xx + (6.0f - 2.0f * B)) * (1.0f / 6.0f);
    if (x < 2.0f)
        return ((-B - 6.0f * C) * xxx + (6.0f * B + 30.0f * C) * xx + (-12.0f * B - 48.0f * C) * x + (8.0f * B + 24.0f * C)) * (1.0f / 6.0f);
    return 0.0f;
}

/*------------------------------------------------------------------------------
    AABB CLIP, based on temporal reprojection anti-aliasing from playdead games
------------------------------------------------------------------------------*/
float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q)
{
    float3 r    = q - p;
    float3 rmax = aabb_max - p;
    float3 rmin = aabb_min - p;

    const float eps = FLT_MIN;
    if (r.x > rmax.x + eps) r *= (rmax.x / r.x);
    if (r.y > rmax.y + eps) r *= (rmax.y / r.y);
    if (r.z > rmax.z + eps) r *= (rmax.z / r.z);
    if (r.x < rmin.x - eps) r *= (rmin.x / r.x);
    if (r.y < rmin.y - eps) r *= (rmin.y / r.y);
    if (r.z < rmin.z - eps) r *= (rmin.z / r.z);

    return p + r;
}

/*------------------------------------------------------------------------------
    CATMULL-ROM HISTORY SAMPLING AT OUTPUT RES
    9-tap catmull-rom collapsed to 5 bilinear taps, reduces the blur caused by
    chaining bilinear filters across frames, based on
    https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
------------------------------------------------------------------------------*/
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

    float3 result = 0.0f.xxx;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_0.x,  tex_pos_0.y),  0.0f).rgb * w0.x  * w0.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_0.y),  0.0f).rgb * w12.x * w0.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_3.x,  tex_pos_0.y),  0.0f).rgb * w3.x  * w0.y;

    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_0.x,  tex_pos_12.y), 0.0f).rgb * w0.x  * w12.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_12.y), 0.0f).rgb * w12.x * w12.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_3.x,  tex_pos_12.y), 0.0f).rgb * w3.x  * w12.y;

    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_0.x,  tex_pos_3.y),  0.0f).rgb * w0.x  * w3.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_3.y),  0.0f).rgb * w12.x * w3.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_3.x,  tex_pos_3.y),  0.0f).rgb * w3.x  * w3.y;

    return max(result, 0.0f.xxx);
}

/*------------------------------------------------------------------------------
    MAIN
------------------------------------------------------------------------------*/
float3 taau(uint2 px_out)
{
    // setup
    float2 res_out;
    tex_uav.GetDimensions(res_out.x, res_out.y);
    float2 uv_out        = (px_out + 0.5f) / res_out;
    float  scale         = buffer_frame.resolution_scale;
    float2 active_render = buffer_frame.resolution_render * scale;
    int2   px_render_max = max(int2(active_render) - 1, int2(0, 0));

    // taa_jitter_current is in ndc, ndc to pixel is (x*0.5*width, y*-0.5*height) because
    // ndc y is flipped relative to texture y, the rendered image was shifted by +jitter_px
    // so the world point that maps to output pixel uv_out lands at uv_out * active + jitter_px
    // in render pixel space
    float2 jitter_px = buffer_frame.taa_jitter_current * float2(0.5f, -0.5f) * active_render;
    float2 p_render  = uv_out * active_render + jitter_px;
    int2   center    = int2(floor(p_render));

    // 3x3 gather, mitchell weighted current reconstruction plus ycocg neighborhood stats
    // plus closest reverse-z depth for velocity fetch, all in one pass
    float3 m1            = 0.0f.xxx;
    float3 m2            = 0.0f.xxx;
    float3 cmin          =  1e10f.xxx;
    float3 cmax          = -1e10f.xxx;
    float3 current_rgb   = 0.0f.xxx;
    float  weight_sum    = 0.0f;
    float  closest_depth = -1.0f;
    int2   closest_pos   = clamp(center, int2(0, 0), px_render_max);

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 tap = clamp(center + int2(dx, dy), int2(0, 0), px_render_max);

            float3 s_rgb   = max(tex2[tap].rgb, 0.0f.xxx);
            float3 s_ycocg = rgb_to_ycocg(s_rgb);

            m1   += s_ycocg;
            m2   += s_ycocg * s_ycocg;
            cmin  = min(cmin, s_ycocg);
            cmax  = max(cmax, s_ycocg);

            float2 d = (float2(center + int2(dx, dy)) + 0.5f) - p_render;
            float  w = mitchell(d.x) * mitchell(d.y);
            current_rgb += s_rgb * w;
            weight_sum  += w;

            float z = tex_depth[tap].r;
            if (z > closest_depth)
            {
                closest_depth = z;
                closest_pos   = tap;
            }
        }
    }

    // current sample, normalized, clamped to a sane hdr range
    current_rgb          = saturate_16(max(current_rgb / max(weight_sum, FLT_MIN), 0.0f.xxx));
    float3 current_ycocg = rgb_to_ycocg(current_rgb);

    // velocity is ndc delta, convert to uv delta with the standard y-flip
    float2 velocity_ndc = tex_velocity[closest_pos].xy;
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    float2 uv_prev      = uv_out - velocity_uv;

    // reset, first frame, or off-screen reprojection, no usable history
    bool history_invalid = reset_history() > 0.5f || !is_valid_uv(uv_prev);
    if (history_invalid)
        return current_rgb;

    // history at output res via catmull-rom, converted to ycocg
    float3 history_rgb   = sample_history_catmull_rom(uv_prev, res_out);
    float3 history_ycocg = rgb_to_ycocg(history_rgb);

    // depth-reproject the closest surface through the previous unjittered vp,
    // pixel disagreement vs the velocity-derived uv_prev is a robust disocclusion proxy
    // get_position internally uses the jittered inverse vp, which leaves a sub-pixel
    // residual, so the threshold is loose enough to tolerate it
    float2 uv_screen        = (float2(closest_pos) + 0.5f) / max(active_render, 1.0f.xx);
    float3 pos_ws           = get_position(closest_depth, uv_screen);
    float4 clip_prev        = mul(float4(pos_ws, 1.0f), buffer_frame.view_projection_previous_unjittered);
    float2 ndc_prev         = clip_prev.xy / max(abs(clip_prev.w), FLT_MIN) * sign(clip_prev.w + FLT_MIN);
    float2 uv_prev_expect   = ndc_prev * float2(0.5f, -0.5f) + 0.5f;
    float  disp_px          = length((uv_prev_expect - uv_prev) * res_out);
    float  confidence_depth = saturate(1.0f - smoothstep(1.5f, 4.0f, disp_px));

    // ycocg mean and stddev, sigma is scaled tighter under low confidence to crush ghosts
    float3 mean     = m1 * RPC_9;
    float3 sigma    = sqrt(abs(m2 * RPC_9 - mean * mean));
    float  gamma    = lerp(0.75f, 1.75f, confidence_depth);
    float3 aabb_min = max(mean - sigma * gamma, cmin);
    float3 aabb_max = min(mean + sigma * gamma, cmax);
    float3 history_clipped = clip_aabb(aabb_min, aabb_max, clamp(mean, aabb_min, aabb_max), history_ycocg);

    // clip distance, second disocclusion signal, large displacements in chroma typically
    // indicate the history belonged to a different surface
    float clip_distance      = length(history_clipped - history_ycocg) / max(length(sigma), FLT_MIN);
    float confidence_clip    = saturate(1.0f - smoothstep(0.75f, 2.5f, clip_distance));
    float confidence         = min(confidence_depth, confidence_clip);

    // adaptive feedback factor
    //   base                  1/16 at deep static accumulation
    //   upsample-aware floor  each output pixel only gets a fresh render-res sample every
    //                         ~1/scale^2 frames, push the floor up to avoid over-smearing
    //   motion boost          tiny push that keeps moving edges crisper
    //   disocclusion          fade toward 1 as confidence drops so stale history is dropped
    float motion = saturate(length(velocity_uv) * 25.0f);
    float blend  = max(1.0f / 16.0f, scale * scale * 0.125f);
    blend       += motion * 0.05f;
    blend        = lerp(blend, 1.0f, 1.0f - confidence);
    blend        = saturate(blend);

    // fsr2 style anti-flicker, weighted blend in tonemapped luma space, brighter samples
    // contribute less which keeps single-frame fireflies from leaking into the accumulator
    float w_c       = 1.0f / (1.0f + max(current_ycocg.x,        0.0f));
    float w_h       = 1.0f / (1.0f + max(history_clipped.x,      0.0f));
    float blend_eff = (w_c * blend) / max(w_c * blend + w_h * (1.0f - blend), FLT_MIN);

    float3 result_ycocg = lerp(history_clipped, current_ycocg, blend_eff);
    float3 result_rgb   = ycocg_to_rgb(result_ycocg);
    return saturate_16(max(result_rgb, 0.0f.xxx));
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= int2(resolution_out)))
        return;

    tex_uav[thread_id.xy] = float4(taau(thread_id.xy), 1.0f);
}
