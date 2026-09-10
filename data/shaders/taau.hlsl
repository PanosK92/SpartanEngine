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

// single-pass, phase-weighted taau with 2,304 bytes of lds per 8x8 group and up to five history samples.
// prior-revision user-reported profiler timings: 0.32-0.40 ms versus approximately 1.46 ms for xess/dlss, using
// roughly 73-78% less gpu time in that setup. reuses existing textures without a mono history copy.

#include "common.hlsl"
#include "shared_taau.h"

static const uint tile_capacity = (TAAU_GROUP_X + 4) * (TAAU_GROUP_Y + 4);
// fp32 prevents compressed highlights rounding to 1; separate planes avoid a four-word lds stride.
groupshared float current_tile_y[tile_capacity];
groupshared float current_tile_co[tile_capacity];
groupshared float current_tile_cg[tile_capacity];
groupshared float current_tile_depth[tile_capacity];

float4 read_current_tile(uint i)
{
    return float4(current_tile_y[i], current_tile_co[i], current_tile_cg[i], current_tile_depth[i]);
}

float reset_history() { return pass_get_f3_value().x; }

static const float sky_depth          = 1e-7f;
static const float reuse_depth_tol = 0.02f;

float3 tonemap_for_taa(float3 c)
{
    float l = max(c.r, max(c.g, c.b));
    return c * rcp(1.0f + l);
}

float3 tonemap_for_taa_inv(float3 c)
{
    float l = max(c.r, max(c.g, c.b));
    return c * rcp(max(1.0f - l, 1.0f / (1.0f + FLT_MAX_16U)));
}

float3 to_ycocg(float3 c)
{
    return float3(dot(c, float3(0.25f, 0.5f, 0.25f)),
                  0.5f * (c.r - c.b), 0.5f * c.g - 0.25f * (c.r + c.b));
}

float3 from_ycocg(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

float4 load_current(int2 px)
{
    float3 rgb = tex2[px].rgb;
    float depth = tex_depth[px].r;
    // negative luminance marks invalid color while preserving depth.
    if (any(isnan(rgb)) || any(isinf(rgb)))
        return float4(-1.0f, 0.0f, 0.0f, depth);
    return float4(to_ycocg(tonemap_for_taa(clamp(rgb, 0.0f.xxx, FLT_MAX_16U.xxx))), depth);
}

int2 render_center(float2 px_out, float2 res_out, float2 res_render)
{
    float2 jitter_px = buffer_frame.taa_jitter_current * float2(0.5f, -0.5f) * res_render;
    return int2(floor(clamp((px_out + 0.5f) / res_out * res_render + jitter_px,
                           0.5f.xx, res_render - 0.5f)));
}

float max3(float3 c)
{
    return max(c.r, max(c.g, c.b));
}

// five-tap catmull-rom approximation with omitted corners and renormalized weights.
float4 sample_history(float2 uv, float2 res, bool depth_edge)
{
    // positive weights prevent neighboring surfaces from ringing into a geometric silhouette.
    [branch]
    if (depth_edge)
        return tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0.0f);

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

    float4 s0 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tc12.x, tc0.y),  0.0f);
    float4 s1 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tc0.x,  tc12.y), 0.0f);
    float4 s2 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tc12.x, tc12.y), 0.0f);
    float4 s3 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tc3.x,  tc12.y), 0.0f);
    float4 s4 = tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tc12.x, tc3.y),  0.0f);

    float k0 = w12.x * w0.y;
    float k1 = w0.x  * w12.y;
    float k2 = w12.x * w12.y;
    float k3 = w3.x  * w12.y;
    float k4 = w12.x * w3.y;

    float  k_sum  = k0 + k1 + k2 + k3 + k4;
    float3 result = s0.rgb * k0 + s1.rgb * k1 + s2.rgb * k2 + s3.rgb * k3 + s4.rgb * k4;
    result        = result * rcp(max(k_sum, 1e-5f));

    // clamp negative-lobe ringing around bright subpixel highlights.
    float3 lo = min(s2.rgb, min(min(s0.rgb, s1.rgb), min(s3.rgb, s4.rgb)));
    float3 hi = max(s2.rgb, max(max(s0.rgb, s1.rgb), max(s3.rgb, s4.rgb)));
    return float4(max(clamp(result, lo, hi), 0.0f.xxx), max(s2.a, 0.0f));
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

    // w=0 removes camera translation, including the stereo eye offset.
    float4 curr_clip = mul(float4(view_dir, 0.0f), vp_curr);
    float4 prev_clip = mul(float4(view_dir, 0.0f), vp_prev);
    return curr_clip.xy / max(curr_clip.w, 1e-6f) - prev_clip.xy / max(prev_clip.w, 1e-6f);
}

float compute_history_reuse(int2 px_render, float2 res_render, int2 px_render_max, float2 uv_prev, float depth_raw, float previous_depth, float moving, bool foliage_coverage, float2 depth_range)
{
    bool is_sky = depth_raw <= sky_depth;
    float expected = abs(previous_depth);
    // older/custom velocity producers may not provide previous surface depth.
    if (!is_sky && (expected <= 0.0f || expected >= FLT_MAX_16U || isnan(expected) || isinf(expected)))
    {
        float2 uv_render = (float2(px_render) + 0.5f) / res_render;
        float3 position = get_position(depth_raw, uv_render);
        float4 prev_clip = mul(float4(position, 1.0f), get_view_projection_previous());
        if (prev_clip.w <= 1e-6f || prev_clip.z < 0.0f || prev_clip.z > prev_clip.w)
            return 0.0f;
        expected = linearize_depth(prev_clip.z / prev_clip.w);
    }

    float2 prev_uv = uv_prev + buffer_frame.taa_jitter_previous * float2(0.5f, -0.5f);
    float2 prev_pos = prev_uv * res_render - 0.5f;
    int2 base = int2(floor(prev_pos));
    float2 f = frac(prev_pos);
    float reuse = 0.0f;
    float coverage = 0.0f;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            int2 tap = base + int2(x, y);
            if (any(tap < 0) || any(tap > px_render_max))
                continue;
            float z = tex3[tap].r;
            float match = 0.0f;
            if (is_sky)
                match = z <= sky_depth ? 1.0f : 0.0f;
            else if (z > sky_depth)
            {
                float error = abs(linearize_depth(z) - expected) / max(expected, 1e-3f);
                match = 1.0f - smoothstep(reuse_depth_tol, 2.0f * reuse_depth_tol, error);
            }
            // cutout jitter alternates visible layers; validate their local depth range instead of one leaf.
            if (foliage_coverage)
                match = max(match, z >= depth_range.x && z <= depth_range.y ? 1.0f : 0.0f);
            float2 w = float2(x == 0 ? 1.0f - f.x : f.x, y == 0 ? 1.0f - f.y : f.y);
            coverage += w.x * w.y * match;
            // stationary coverage can survive a jitter phase; moving coverage must actually match.
            if (w.x * w.y > 0.01f)
                reuse = max(reuse, match);
        }
    }
    // retain partial surface coverage proportionally instead of treating one matching tap as full support.
    return saturate(lerp(reuse, coverage, moving));
}

float4 taau(uint2 px_out, float2 res_out, int2 tile_origin, uint tile_width, bool use_tile)
{
    float2 uv_out        = (px_out + 0.5f) / res_out;
    uint2  active_render = uint2(get_render_resolution_active());
    int2   px_render_max = max(int2(active_render) - 1, int2(0, 0));

    float2 active_render_f = float2(active_render);
    float2 jitter_px       = buffer_frame.taa_jitter_current * float2(0.5f, -0.5f) * active_render_f;
    float2 p_render        = uv_out * active_render_f + jitter_px;

    p_render      = clamp(p_render, float2(0.5f, 0.5f), active_render_f - 0.5f);
    int2 center   = clamp(int2(floor(p_render)), int2(0, 0), px_render_max);

    int2 base = int2(floor(p_render - 0.5f));
    float2 f = p_render - (float2(base) + 0.5f);
    float2 f2 = f * f;
    float2 f3 = f2 * f;
    // all four cubic taps are needed to preserve linear ramps across jitter phases.
    float2 w0 = f2 - 0.5f * (f3 + f);
    float2 w1 = 1.5f * f3 - 2.5f * f2 + 1.0f;
    float2 w3 = 0.5f * (f3 - f2);
    float2 w2 = 1.0f - w0 - w1 - w3;
    float4 wx = float4(w0.x, w1.x, w2.x, w3.x);
    float4 wy = float4(w0.y, w1.y, w2.y, w3.y);

    float3 color_min      =  FLT_MAX_16U.xxx;
    float3 color_max      = -FLT_MAX_16U.xxx;
    float3 reconstruction_min = FLT_MAX_16U.xxx;
    float3 reconstruction_max = -FLT_MAX_16U.xxx;
    float3 moment1        = 0.0f.xxx;
    float3 moment2        = 0.0f.xxx;
    float  sample_count   = 0.0f;
    float3 current_ycocg  = 0.0f.xxx;
    float4 current_linear = 0.0f.xxxx;
    float3 coverage_min = FLT_MAX_16U.xxx;
    float3 coverage_max = 0.0f.xxx;
    float neighborhood_peak_tm = 0.0f;
    float neighborhood_floor_tm = 1.0f;
    float  weight_sum     = 0.0f;

    int2  closest_px    = center;
    float closest_depth = -1.0f;
    float furthest_depth = 1.0f;

    [unroll]
    for (int oy = -1; oy <= 2; ++oy)
    {
        [unroll]
        for (int ox = -1; ox <= 2; ++ox)
        {
            int2 tap = base + int2(ox, oy);
            if (any(tap < 0) || any(tap > px_render_max))
            {
                continue;
            }

            uint tile_index = uint(tap.y - tile_origin.y) * tile_width + uint(tap.x - tile_origin.x);
            float4 sample_data;
            [branch]
            if (use_tile)
                sample_data = read_current_tile(tile_index);
            else
                sample_data = load_current(tap);
            if (sample_data.x < 0.0f)
                continue;
            float3 ycocg = sample_data.xyz;
            float w = wx[ox + 1] * wy[oy + 1];
            current_ycocg += ycocg * w;
            weight_sum += w;
            if (ox >= 0 && ox <= 1 && oy >= 0 && oy <= 1)
            {
                reconstruction_min = min(reconstruction_min, ycocg);
                reconstruction_max = max(reconstruction_max, ycocg);
                float coverage_weight = (ox == 0 ? 1.0f - f.x : f.x) * (oy == 0 ? 1.0f - f.y : f.y);
                float3 sample_linear = tonemap_for_taa_inv(max(from_ycocg(ycocg), 0.0f.xxx));
                current_linear += float4(sample_linear, 1.0f) * coverage_weight;
                coverage_min = min(coverage_min, sample_linear);
                coverage_max = max(coverage_max, sample_linear);
            }

            // rectification must see every sample used to reconstruct thin texture lines.
            color_min = min(color_min, ycocg);
            color_max = max(color_max, ycocg);
            moment1 += ycocg;
            moment2 += ycocg * ycocg;
            sample_count += 1.0f;

            // keep motion dilation local so distant surfaces do not steer reprojection.
            if (all(abs(tap - center) <= 1))
            {
                neighborhood_peak_tm = max(neighborhood_peak_tm, ycocg.x + max(ycocg.z, abs(ycocg.y) - ycocg.z));
                neighborhood_floor_tm = min(neighborhood_floor_tm, ycocg.x + min(ycocg.z, -abs(ycocg.y) - ycocg.z));
                furthest_depth = min(furthest_depth, sample_data.w);
                if (sample_data.w > closest_depth)
                {
                    closest_depth = sample_data.w;
                    closest_px = tap;
                }
            }
        }
    }

    bool current_valid = weight_sum > 1e-5f && sample_count > 0.0f;
    // clamp to the interpolation footprint, not distant cubic taps that can create an outline.
    if (reconstruction_min.x > reconstruction_max.x)
    {
        reconstruction_min = color_min;
        reconstruction_max = color_max;
    }
    current_ycocg = current_valid ? clamp(current_ycocg * rcp(weight_sum), reconstruction_min, reconstruction_max) : 0.0f.xxx;
    float3 current_rgb_tm = max(from_ycocg(current_ycocg), 0.0f.xxx);

    float4 center_data;
    [branch]
    if (use_tile)
        center_data = read_current_tile(uint(center.y - tile_origin.y) * tile_width + uint(center.x - tile_origin.x));
    else
        center_data = load_current(center);
    float center_depth = center_data.w;
    bool depth_edge = closest_depth - furthest_depth > max(closest_depth * reuse_depth_tol, sky_depth);
    // resets and bootstrap must reconstruct geometric coverage before compressing radiance.
    if (depth_edge && current_linear.a > 1e-5f)
    {
        current_rgb_tm = tonemap_for_taa(current_linear.rgb / current_linear.a);
        current_ycocg = to_ycocg(current_rgb_tm);
    }
    float4 center_velocity = tex_velocity[center];
    float4 closest_velocity = depth_edge ? tex_velocity[closest_px] : center_velocity;
    bool foliage_coverage = center_velocity.w < 0.0f || closest_velocity.w < 0.0f;
    bool   is_sky          = center_depth <= sky_depth;
    float2 velocity_ndc;
    if (is_sky)
    {
        velocity_ndc = center_velocity.xy;
        if (dot(velocity_ndc, velocity_ndc) < 1e-12f)
            velocity_ndc = compute_sky_velocity(p_render / active_render_f);
    }
    else
    {
        // motion and depth must refer to the same sample that supplies the reconstructed color.
        velocity_ndc = center_velocity.xy;
    }
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    float2 uv_prev      = uv_out - velocity_uv;

    float2 inset           = 0.5f / res_out;
    bool   uv_prev_valid   = all(uv_prev >= inset) && all(uv_prev <= 1.0f - inset);
    bool   history_invalid = reset_history() > 0.5f || !uv_prev_valid;

    if (!current_valid)
    {
        return 0.0f.xxxx;
    }

    if (history_invalid)
    {
        return float4(saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx)), 0.0f);
    }

    float motion_px = length(velocity_uv * res_out);
    // stationary background can still be uncovered by a moving foreground edge.
    if (depth_edge)
        motion_px = max(motion_px, length(closest_velocity.xy * float2(0.5f, -0.5f) * res_out));
    float moving_coverage = saturate(motion_px * (depth_edge ? 8.0f : 2.0f));
    // subpixel motion also resamples history, so it needs a shorter accumulation window.
    float motion    = saturate(motion_px * 8.0f);

    float3 mean = moment1 / sample_count;
    float3 sigma = sqrt(max(moment2 / sample_count - mean * mean, 0.0f.xxx));
    float clip_motion = saturate(motion_px * 0.5f);
    float gamma = lerp(2.5f, 1.0f, clip_motion);
    float3 box_min = max(color_min, mean - gamma * sigma);
    float3 box_max = min(color_max, mean + gamma * sigma);
    box_min = min(box_min, current_ycocg);
    box_max = max(box_max, current_ycocg);

    float level = max3(current_rgb_tm);
    // allow static shadow noise to accumulate instead of clipping history to each new sample.
    float noise_pad = depth_edge ? 0.0f : max(0.0001f, 0.05f * level * (1.0f - level)) * (1.0f - clip_motion);
    box_min.x -= noise_pad;
    box_max.x += noise_pad;

    // depth belongs to the selected render sample, not the output pixel between samples.
    float2 surface_uv_prev = uv_prev + (float2(center) + 0.5f - p_render) / active_render_f;
    float reuse = compute_history_reuse(center, active_render_f, px_render_max, surface_uv_prev, center_depth, center_velocity.w, moving_coverage, foliage_coverage, float2(furthest_depth, closest_depth));
    if (reuse <= 0.0f)
    {
        return float4(saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx)), 0.0f);
    }

    float4 history_sample;
    [branch]
    if (all(velocity_uv == 0.0f.xx))
        history_sample = tex[px_out];
    else
        history_sample = sample_history(uv_prev, res_out, depth_edge);
    float3 history_rgb = history_sample.rgb;
    if (any(isnan(history_sample)) || any(isinf(history_sample)))
    {
        return float4(saturate_16(max(tonemap_for_taa_inv(current_rgb_tm), 0.0f.xxx)), 0.0f);
    }

    float3 history_tm = tonemap_for_taa(history_rgb);
    float3 history_ycocg = to_ycocg(history_tm);
    float2 scale = res_out / active_render_f;
    float2 distance_out = abs((float2(center) + 0.5f - p_render) * scale);
    float2 kernel = saturate(1.0f - distance_out);
    float sample_weight = kernel.x * kernel.y;
    float sample_area = max(scale.x * scale.y, 1.0f);
    // stationary texture detail needs a variance margin independent of the current jitter phase.
    // geometric edges keep strict bounds so this margin cannot preserve a neighboring surface's outline.
    float3 clip_pad = depth_edge ? 0.0f.xxx : sigma * (1.0f - motion);
    float3 clipped_ycocg;
    float3 history_clipped_tm;
    if (depth_edge && current_linear.a > 1e-5f)
    {
        // chroma-box rectification can darken valid mixed coverage; bound silhouettes in radiance instead.
        float3 coverage_center = current_linear.rgb / current_linear.a;
        float3 edge_min = lerp(coverage_min, coverage_center, 0.25f * clip_motion);
        float3 edge_max = lerp(coverage_max, coverage_center, 0.25f * clip_motion);
        // preserve stationary highlights supported by nearby jitter samples without relaxing the dark silhouette bound.
        float neighborhood_peak = neighborhood_peak_tm / max(1.0f - neighborhood_peak_tm, 1.0f / (1.0f + FLT_MAX_16U));
        edge_max = lerp(edge_max, max(edge_max, min(history_rgb, neighborhood_peak.xxx)), 1.0f - (foliage_coverage ? clip_motion : motion));
        if (foliage_coverage)
        {
            // a leaf can leave the central footprint for one jitter phase without leaving the pixel coverage.
            float neighborhood_floor = max(neighborhood_floor_tm, 0.0f) / max(1.0f - neighborhood_floor_tm, 1.0f / (1.0f + FLT_MAX_16U));
            edge_min = lerp(edge_min, min(edge_min, max(history_rgb, neighborhood_floor.xxx)), 1.0f - clip_motion);
        }
        history_clipped_tm = tonemap_for_taa(clip_to_aabb(edge_min, edge_max, history_rgb));
        clipped_ycocg = to_ycocg(history_clipped_tm);
    }
    else
    {
        float3 clip_min = box_min - clip_pad;
        float3 clip_max = box_max + clip_pad;
        // stationary chroma corrections must not drag valid grout luminance toward the box center.
        clipped_ycocg = clip_to_aabb(clip_min, clip_max, history_ycocg);
        if (!is_sky && !depth_edge)
            clipped_ycocg.x = lerp(clamp(history_ycocg.x, clip_min.x, clip_max.x), clipped_ycocg.x, motion);
        history_clipped_tm = max(from_ycocg(clipped_ycocg), 0.0f.xxx);
    }

    float rejection = saturate(max3(abs(history_ycocg - clipped_ycocg)) /
                              max(max3(box_max - box_min), 0.02f));
    // accumulate reprojected samples while moving as well as at rest; motion is not a reset.
    float max_weight = lerp(128.0f, 8.0f, motion) / sample_area;
    float retained_weight = min(history_sample.a, max_weight) * reuse;
    // clipping repairs the history color; reduce its confidence in proportion to that repair.
    retained_weight *= 1.0f - 0.75f * rejection;
    float total_weight = retained_weight + sample_weight;
    float sample_blend = sample_weight / max(total_weight, 1e-5f);
    float3 point_rgb_tm = center_data.x >= 0.0f ? max(from_ycocg(center_data.xyz), 0.0f.xxx) : current_rgb_tm;
    // use reconstruction while the first few valid jitter samples establish coverage.
    float confidence = saturate(retained_weight * sample_area * 0.25f) * reuse;
    // every coverage blend must use radiance, including bootstrap and confidence blending.
    if (depth_edge)
    {
        float3 history_linear = tonemap_for_taa_inv(history_clipped_tm);
        float3 accumulated_linear = lerp(history_linear, tonemap_for_taa_inv(point_rgb_tm), sample_blend);
        float3 bootstrap_linear = lerp(history_linear, tonemap_for_taa_inv(current_rgb_tm), lerp(1.0f, 0.125f, reuse));
        return float4(saturate_16(max(lerp(bootstrap_linear, accumulated_linear, confidence), 0.0f.xxx)), min(total_weight, max_weight));
    }
    float3 accumulated_tm = lerp(history_clipped_tm, point_rgb_tm, sample_blend);
    // uncovered pixels must recover even when this jitter phase contributes no point sample.
    float3 bootstrap_tm = lerp(history_clipped_tm, current_rgb_tm, lerp(1.0f, 0.125f, reuse));
    float3 result_rgb_tm = max(lerp(bootstrap_tm, accumulated_tm, confidence), 0.0f.xxx);
    return float4(saturate_16(tonemap_for_taa_inv(result_rgb_tm)), min(total_weight, max_weight));
}

[numthreads(TAAU_GROUP_X, TAAU_GROUP_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID, uint lane : SV_GroupIndex)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    float2 res_render = get_render_resolution_active();
    // downsampling can exceed the fixed tile capacity; this branch is uniform across the group.
    bool use_tile = all(res_render <= resolution_out);
    uint2 group_start = group_id.xy * uint2(TAAU_GROUP_X, TAAU_GROUP_Y);
    int2 tile_origin = render_center(float2(group_start), resolution_out, res_render) - 2;
    int2 tile_end = render_center(float2(group_start + uint2(TAAU_GROUP_X - 1, TAAU_GROUP_Y - 1)), resolution_out, res_render) + 2;
    uint2 tile_size = uint2(tile_end - tile_origin + 1);
    [branch]
    if (use_tile)
    {
        for (uint i = lane; i < tile_size.x * tile_size.y; i += TAAU_GROUP_X * TAAU_GROUP_Y)
        {
            int2 px = tile_origin + int2(i % tile_size.x, i / tile_size.x);
            float4 data = float4(-1.0f, 0.0f, 0.0f, 0.0f);
            if (all(px >= 0) && all(px < int2(res_render)))
                data = load_current(px);
            current_tile_y[i] = data.x;
            current_tile_co[i] = data.y;
            current_tile_cg[i] = data.z;
            current_tile_depth[i] = data.w;
        }
        // out-of-bounds output lanes must reach this barrier before returning.
        GroupMemoryBarrierWithGroupSync();
    }
    if (any(thread_id.xy >= uint2(resolution_out)))
        return;

    float4 result = taau(thread_id.xy, resolution_out, tile_origin, tile_size.x, use_tile);
    tex_uav[thread_id.xy] = float4(result.rgb, pass_get_f3_value().y > 0.5f ? 1.0f : result.a);
    // y enables the mono history write into the post-process scratch.
    if (pass_get_f3_value().y > 0.5f)
        tex_uav2[thread_id.xy] = result;
}
