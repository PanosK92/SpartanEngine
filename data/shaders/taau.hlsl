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
//      blackman-harris weighted 3x3 render-res gather that accounts for the
//      projection jitter, this is the actual upsampling step
//   2. fetch reprojected history at output res via a 5-tap catmull-rom
//   3. clip history to a tonemapped ycocg variance aabb derived from the same 3x3
//      to suppress ghosting and to handle disocclusion implicitly via clip distance
//   4. blend with a small fixed feedback factor in the tonemapped space, then
//      untonemap so downstream post-processing still sees hdr
// bindings, set by the renderer pass:
//   tex          history at output resolution
//   tex2         current jittered color at render resolution, only the top-left
//                resolution_render * resolution_scale region is valid
//   tex_depth    reverse-z depth at render resolution, used only to pick the closest
//                surface for velocity fetch
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
    HDR TONEMAP, the variance stats, aabb clip and history blend all happen in
    this compressed [0,1) space so a single bright firefly in the 3x3 cannot
    inflate cmax/sigma and let stale history flicker through, the inverse is
    applied on the way out so post-processing still operates on hdr values
------------------------------------------------------------------------------*/
float3 tonemap_for_taa(float3 c)
{
    float l = max(c.r, max(c.g, c.b));
    return c * rcp(1.0f + l);
}

float3 tonemap_for_taa_inv(float3 c)
{
    float l = max(c.r, max(c.g, c.b));
    return c * rcp(max(1.0f - l, 1e-5f));
}

/*------------------------------------------------------------------------------
    BLACKMAN-HARRIS RECONSTRUCTION WEIGHT
    non-negative gaussian-like kernel (~3x3 support), used by fsr/smaa-t2x for
    sub-pixel sample reconstruction, replaces mitchell-netravali whose negative
    outer lobes sit at distance ~1.5 and get partially truncated by a 3x3 gather,
    producing asymmetric, frame-varying weight sums that boil on fine detail
------------------------------------------------------------------------------*/
float reconstruct_weight(float d_sq)
{
    return exp(-2.29f * d_sq);
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
    chaining bilinear filters across frames
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

    // taa_jitter_current is in ndc, ndc to render-pixel is (x*0.5*width, y*-0.5*height)
    // because ndc y is flipped relative to texture y, the rendered image is shifted by
    // +jitter_px so the world point that corresponds to output pixel uv_out lands at
    // uv_out*active_render + jitter_px in render pixel space
    float2 jitter_px = buffer_frame.taa_jitter_current * float2(0.5f, -0.5f) * active_render;
    float2 p_render  = uv_out * active_render + jitter_px;

    // clamp the reconstruction point so the 3x3 gather always centers inside the
    // valid (top-left) rendered region, near the right and bottom output edges
    // p_render would otherwise land outside the rasterized area and skew weights
    p_render         = clamp(p_render, float2(0.5f, 0.5f), float2(active_render) - 0.5f);
    int2   center    = clamp(int2(floor(p_render)), int2(0, 0), px_render_max);

    // 3x3 gather, blackman-harris weighted reconstruction in tonemapped space plus
    // tonemapped ycocg neighborhood stats plus closest reverse-z depth for velocity
    // fetch, all in one pass
    float3 m1             = 0.0f.xxx;
    float3 m2             = 0.0f.xxx;
    float3 cmin           =  FLT_MAX_16U.xxx;
    float3 cmax           = -FLT_MAX_16U.xxx;
    float3 current_rgb_tm = 0.0f.xxx;
    float  weight_sum     = 0.0f;

    // pre-seed closest-depth tracking with the central tap so ties (e.g. flat sky)
    // resolve to the center, otherwise the first unrolled iteration always wins
    float closest_depth = tex_depth[center].r;
    int2  closest_pos   = center;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 tap = clamp(center + int2(dx, dy), int2(0, 0), px_render_max);

            float3 s_rgb      = max(tex2[tap].rgb, 0.0f.xxx);
            float3 s_rgb_tm   = tonemap_for_taa(s_rgb);
            float3 s_ycocg_tm = rgb_to_ycocg(s_rgb_tm);

            m1   += s_ycocg_tm;
            m2   += s_ycocg_tm * s_ycocg_tm;
            cmin  = min(cmin, s_ycocg_tm);
            cmax  = max(cmax, s_ycocg_tm);

            float2 d = (float2(center + int2(dx, dy)) + 0.5f) - p_render;
            float  w = reconstruct_weight(dot(d, d));
            current_rgb_tm += s_rgb_tm * w;
            weight_sum     += w;

            float z = tex_depth[tap].r;
            if (z > closest_depth)
            {
                closest_depth = z;
                closest_pos   = tap;
            }
        }
    }

    // current reconstructed sample in tonemapped space, the kernel is non-negative
    // so weight_sum is strictly positive across the full 3x3 and the divide is safe
    current_rgb_tm          = current_rgb_tm * rcp(weight_sum);
    float3 current_ycocg_tm = rgb_to_ycocg(current_rgb_tm);

    // velocity is stored as an ndc delta, convert to a uv delta, the y axis flips
    // because ndc y is bottom-up while uv y is top-down
    float2 velocity_ndc = tex_velocity[closest_pos].xy;
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    float2 uv_prev      = uv_out - velocity_uv;

    // reset, first frame, or off-screen reprojection, no usable history
    bool history_invalid = reset_history() > 0.5f || !is_valid_uv(uv_prev);
    if (history_invalid)
        return saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx));

    // history at output res via catmull-rom, tonemapped to match the clip space
    float3 history_rgb      = sample_history_catmull_rom(uv_prev, res_out);
    float3 history_rgb_tm   = tonemap_for_taa(history_rgb);
    float3 history_ycocg_tm = rgb_to_ycocg(history_rgb_tm);

    // ycocg mean and stddev in tonemapped space, the aabb is built from sigma and
    // intersected with the raw min/max so narrow neighborhoods still allow some
    // detail through, gamma stays loose at rest so detail accumulates and only
    // tightens under motion to crush ghosting on moving edges, the motion ramp
    // is in output pixels per frame, slower than the previous version so subtle
    // panning does not flick history away on every frame
    float3 mean      = m1 * RPC_9;
    float3 sigma     = sqrt(max(m2 * RPC_9 - mean * mean, 0.0f.xxx));
    float  motion    = saturate(length(velocity_uv) * res_out.x * (1.0f / 64.0f));
    float  gamma     = lerp(1.5f, 1.0f, motion);
    float3 aabb_min  = max(mean - sigma * gamma, cmin);
    float3 aabb_max  = min(mean + sigma * gamma, cmax);
    float3 history_c = clip_aabb(aabb_min, aabb_max, clamp(mean, aabb_min, aabb_max), history_ycocg_tm);

    // fixed feedback factor, the aabb clip above already handles disocclusion
    // implicitly by snapping stale history to the current neighborhood, so a
    // separate disocclusion-driven blend lift would just fight the accumulator
    // and produce content-dependent ringing
    float blend = 1.0f / 8.0f;

    float3 result_ycocg_tm = lerp(history_c, current_ycocg_tm, blend);
    float3 result_rgb_tm   = ycocg_to_rgb(result_ycocg_tm);
    // saturate in tonemapped space, ycocg->rgb on a clipped box can drift slightly
    // outside [0,1] and tonemap_for_taa_inv divides by (1 - max(c)) which would
    // otherwise blow up for max(c) >= 1
    result_rgb_tm          = saturate(result_rgb_tm);
    float3 result_rgb      = tonemap_for_taa_inv(result_rgb_tm);
    return saturate_16(result_rgb);
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
