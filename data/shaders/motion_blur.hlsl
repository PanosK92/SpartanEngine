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

//= includes =========
#include "common.hlsl"
//====================

// tuning parameters
static const int   SAMPLE_COUNT           = 11;    // samples per direction (total = 2 * count + 1 = 23)
static const float MAX_BLUR_RADIUS_PIXELS = 48.0f; // maximum blur extent in pixels
static const float MIN_BLUR_THRESHOLD     = 0.75f; // minimum velocity in pixels to trigger blur
static const float DEPTH_SCALE            = 50.0f; // depth comparison sensitivity
static const float CENTER_WEIGHT          = 1.5f;  // weight for center sample (higher = sharper center)

// reference frame time - motion blur is normalized to 60fps baseline
// this ensures consistent blur regardless of actual framerate
static const float TARGET_FRAME_TIME = 1.0f / 60.0f;

// artistic control - prevents excessive blur from slow shutter speeds
// without this, dark scenes with 1/30s shutter would have 2x blur which looks bad
static const float MAX_SHUTTER_RATIO = 0.8f;

// neighborhood max search radius in pixels, used to stabilize blur length
// without snapping to a fixed tile grid (which produces visible square artifacts)
static const float NEIGHBORHOOD_RADIUS_PIXELS = 16.0f;

// radial motion blur, hub association range relative to the projected wheel radius
static const float RADIAL_HUB_RANGE_SCALE = 1.35f;
static const float MAX_RADIAL_ANGLE      = 1.0f; // radians per blur direction, keeps arcs tasteful at high rpm

// soft depth comparison (guerrilla games / killzone approach)
// returns 1 when depth_a is behind or equal to depth_b
float soft_depth_compare(float depth_a, float depth_b)
{
    return saturate(1.0f - (depth_a - depth_b) * DEPTH_SCALE);
}

// sample weight falloff - samples further from center contribute less
// uses a smooth gaussian-like falloff for natural blur edges
float sample_falloff(float t)
{
    // hermite smoothstep gives nice soft edges
    float inv_t = 1.0f - t;
    return inv_t * inv_t * (3.0f - 2.0f * inv_t);
}

// velocity magnitude weight - determines how much a sample should contribute
// based on its own velocity vs the center velocity
float velocity_weight(float sample_velocity_length, float center_velocity_length, float sample_distance)
{
    // sample contributes if its blur would reach the center pixel
    float coverage = sample_velocity_length - sample_distance;
    return saturate(coverage / (center_velocity_length + FLT_MIN));
}

// find the dominant velocity in a neighborhood centered on this pixel
// the search is anchored to the pixel position rather than a snapped tile grid,
// so adjacent pixels see almost the same neighborhood and the result varies
// smoothly across the screen instead of producing visible square tile boundaries
float2 get_neighborhood_max_velocity(float2 uv, float2 texel_size, float jitter)
{
    float2 search_step = NEIGHBORHOOD_RADIUS_PIXELS * texel_size;

    // small per-pixel rotation of the sample pattern breaks any residual grid
    // alignment and lets taa average out the tiny remaining differences
    float  angle = jitter * 6.2831853f;
    float  cs    = cos(angle);
    float  sn    = sin(angle);
    float2x2 rot = float2x2(cs, -sn, sn, cs);

    float2 best_velocity = float2(0.0f, 0.0f);
    float  best_length   = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset      = mul(rot, float2(x, y) * search_step);
            float2 sample_uv   = uv + offset;
            float2 sample_vel  = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv * get_render_uv_scale(), 0).xy;
            float  sample_len  = length(sample_vel);

            if (sample_len > best_length)
            {
                best_velocity = sample_vel;
                best_length   = sample_len;
            }
        }
    }

    return best_velocity;
}

// sky pixels have no geometry so the g-buffer velocity is zero - reconstruct
// camera-rotation velocity by reprojecting the view direction with the previous
// frame's view-projection matrix. only rotation matters since the sky is at
// infinity - camera translation (wasd) should not produce sky velocity.
float2 compute_sky_velocity(float2 uv)
{
    // per-eye matrices: this pass runs inside the per-eye compute loop so
    // buffer_pass.eye_index selects the correct eye via the get_* helpers
    matrix vp_curr_inv = get_view_projection_inverted();
    matrix vp_curr     = pass_is_right_eye() ? buffer_frame.view_projection_unjittered_right          : buffer_frame.view_projection_unjittered;
    matrix vp_prev     = pass_is_right_eye() ? buffer_frame.view_projection_previous_unjittered_right : buffer_frame.view_projection_previous_unjittered;

    // reconstruct view direction from the pixel's ndc position
    float2 ndc      = uv_to_ndc(uv);
    float4 clip     = float4(ndc, 0.0001f, 1.0f);
    float4 world    = mul(clip, vp_curr_inv);
    float3 view_dir = normalize(world.xyz / world.w - get_camera_position());

    // place the direction at a fixed large distance from each frame's camera position
    // this cancels out any translation between frames, isolating pure rotation
    static const float sky_distance = 10000.0f;
    float3 sky_point_curr = get_camera_position()                 + view_dir * sky_distance;
    float3 sky_point_prev = buffer_frame.camera_position_previous + view_dir * sky_distance;

    // project through each frame's unjittered matrix
    float4 curr_clip = mul(float4(sky_point_curr, 1.0f), vp_curr);
    float2 curr_ndc  = curr_clip.xy / curr_clip.w;

    float4 prev_clip = mul(float4(sky_point_prev, 1.0f), vp_prev);
    float2 prev_ndc  = prev_clip.xy / prev_clip.w;

    return curr_ndc - prev_ndc;
}

// wheel hubs are supplied by the cpu in buffer_frame.radial_blur_hubs, each entry holds
// screen uv (xy), signed per-frame rotation angle in radians (z) and projected wheel
// radius in output pixels (w), derived from entity transforms, so fast spins do not
// suffer from the screen-space velocity aliasing that breaks vector-based estimation
bool find_radial_hub(float2 uv, float2 resolution, out float4 hub)
{
    hub             = 0.0f;
    float best_dist = 1e10f;
    uint  count     = (uint)buffer_frame.radial_blur_hub_count;

    for (uint i = 0; i < count; ++i)
    {
        float4 candidate = buffer_frame.radial_blur_hubs[i];
        float  dist      = length((uv - candidate.xy) * resolution);
        if (dist < candidate.w * RADIAL_HUB_RANGE_SCALE && dist < best_dist)
        {
            best_dist = dist;
            hub       = candidate;
        }
    }

    return best_dist < 1e10f;
}

// rigid 2d reconstruction for spinning wheels - the wheel's screen motion is rotation
// around the hub plus the hub's own translation, so samples are taken along that path
float4 motion_blur_radial_reconstruction(
    float2 uv,
    float2 resolution,
    float  shutter_ratio,
    float  noise,
    float4 center_color,
    float  center_depth,
    float4 hub
)
{
    float2 hub_uv     = hub.xy;
    float  angle_blur = clamp(hub.z * shutter_ratio, -MAX_RADIAL_ANGLE, MAX_RADIAL_ANGLE);

    // spin is zero at the center, so the velocity at the hub is the wheel's pure translation
    float2 hub_vel_ndc  = tex_velocity.SampleLevel(samplers[sampler_point_clamp], hub_uv * get_render_uv_scale(), 0).xy;
    float2 hub_shift_uv = hub_vel_ndc * float2(0.5f, -0.5f) * shutter_ratio;
    float  shift_pixels = length(hub_shift_uv * resolution);
    if (shift_pixels > MAX_BLUR_RADIUS_PIXELS)
    {
        hub_shift_uv *= MAX_BLUR_RADIUS_PIXELS / shift_pixels;
        shift_pixels  = MAX_BLUR_RADIUS_PIXELS;
    }

    float2 d_pixels      = (uv - hub_uv) * resolution;
    float  radius_pixels = length(d_pixels);
    float  arc_pixels    = abs(angle_blur) * radius_pixels;

    if (arc_pixels + shift_pixels < MIN_BLUR_THRESHOLD)
    {
        return center_color;
    }

    float4 color_sum  = center_color * CENTER_WEIGHT;
    float  weight_sum = CENTER_WEIGHT;
    float  jitter     = (noise - 0.5f) * 0.5f;

    [unroll]
    for (int i = 1; i <= SAMPLE_COUNT; ++i)
    {
        float t          = (float)i / (float)SAMPLE_COUNT;
        float t_jittered = saturate(t + jitter / (float)SAMPLE_COUNT);
        float falloff    = sample_falloff(t);

        [unroll]
        for (int dir = -1; dir <= 1; dir += 2)
        {
            float  step_t    = t_jittered * dir;
            float  ang       = angle_blur * step_t;
            float  cs        = cos(ang);
            float  sn        = sin(ang);
            float2 rotated   = float2(d_pixels.x * cs - d_pixels.y * sn, d_pixels.x * sn + d_pixels.y * cs);
            float2 uv_sample = hub_uv + hub_shift_uv * step_t + rotated / resolution;

            if (!is_valid_uv(uv_sample))
            {
                continue;
            }

            float4 sample_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_sample, 0);
            float  sample_depth = get_linear_depth(uv_sample * get_render_uv_scale());
            float  sample_mask  = tex_velocity.SampleLevel(samplers[sampler_point_clamp], uv_sample * get_render_uv_scale(), 0).z;

            // symmetric depth similarity plus a hard gate to the wheel mask so the arc
            // never drags the sharp body or background across the wheel
            float depth_sym = soft_depth_compare(center_depth, sample_depth) * soft_depth_compare(sample_depth, center_depth);
            float weight    = falloff * depth_sym * sample_mask;

            color_sum  += sample_color * weight;
            weight_sum += weight;
        }
    }

    float4 result = color_sum / weight_sum;
    result.a = center_color.a;
    return result;
}

// main reconstruction filter
float4 motion_blur_reconstruction(
    float2 uv,
    float2 pixel_coord,
    float2 resolution,
    float  shutter_ratio,
    float  noise
)
{
    float2 texel_size = 1.0f / resolution;

    // sample center
    float4 center_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0);
    float2 render_uv    = uv * get_render_uv_scale();
    float  center_depth = get_linear_depth(render_uv);

    // per-pixel velocity - for sky pixels (no geometry), derive from camera rotation
    float  raw_depth       = get_depth(render_uv);
    float4 velocity_sample = tex_velocity.SampleLevel(samplers[sampler_point_clamp], render_uv, 0);
    float2 pixel_velocity  = velocity_sample.xy;
    bool   is_sky          = raw_depth < 0.0001f;
    bool   is_radial       = velocity_sample.z > 0.5f;
    if (is_sky)
    {
        pixel_velocity = compute_sky_velocity(uv);
        is_radial      = false;
    }

    // debug view, r.motion_blur = 2 shows the radial mask in red and hub association in green
    if (pass_get_f3_value().y > 1.5f)
    {
        float4 debug_color = center_color;
        float4 debug_hub;
        if (is_radial)
        {
            debug_color.rgb = lerp(debug_color.rgb, float3(1.0f, 0.0f, 0.0f), 0.5f);
        }
        if (find_radial_hub(uv, resolution, debug_hub))
        {
            debug_color.rgb = lerp(debug_color.rgb, float3(0.0f, 1.0f, 0.0f), 0.25f);
            if (length((uv - debug_hub.xy) * resolution) < 4.0f)
            {
                debug_color.rgb = float3(0.0f, 0.0f, 1.0f);
            }
        }
        return debug_color;
    }

    // radial materials (wheels) rotate around a cpu-supplied hub, when the wheel is not
    // rotating this frame no hub exists and the pixel falls through to the linear path
    if (is_radial)
    {
        float4 hub;
        if (find_radial_hub(uv, resolution, hub))
        {
            return motion_blur_radial_reconstruction(uv, resolution, shutter_ratio, noise, center_color, center_depth, hub);
        }
    }

    float pixel_speed = length(pixel_velocity * resolution);

    // neighborhood dominant velocity, sampled around the pixel itself rather than
    // snapped to a fixed tile grid, which avoids visible square boundaries
    float2 neighborhood_velocity = get_neighborhood_max_velocity(uv, texel_size, noise);
    float  neighborhood_speed    = length(neighborhood_velocity * resolution);

    // use the pixel's own direction but blend the magnitude toward the neighborhood max
    // the boost is gated by similarity to prevent silhouette pixels next to fast-moving
    // backgrounds from inheriting the background's blur length and smearing across it
    float2 velocity;
    if (pixel_speed > MIN_BLUR_THRESHOLD)
    {
        float2 pixel_dir = pixel_velocity / (length(pixel_velocity) + FLT_MIN);
        float2 neigh_dir = neighborhood_velocity / (length(neighborhood_velocity) + FLT_MIN);

        // motion is coherent when this pixel and its neighborhood agree on direction
        // and have comparable speeds, opposite directions cannot stabilize each other
        float dir_agreement = saturate(dot(pixel_dir, neigh_dir));
        float speed_ratio   = pixel_speed / (neighborhood_speed + FLT_MIN);
        float coherence     = dir_agreement * smoothstep(0.5f, 1.0f, speed_ratio);

        float stable_speed = lerp(pixel_speed, neighborhood_speed, 0.7f * coherence);
        velocity = pixel_dir * (stable_speed / resolution);
    }
    else
    {
        velocity = pixel_velocity;
    }

    // convert to pixels
    float2 velocity_pixels = velocity * resolution;
    float  blur_length_raw = length(velocity_pixels);

    // early exit for static pixels
    if (blur_length_raw < MIN_BLUR_THRESHOLD)
        return center_color;

    // apply shutter ratio
    float blur_length = blur_length_raw * shutter_ratio;

    // exit if blur became too small after adjustments
    if (blur_length < MIN_BLUR_THRESHOLD)
        return center_color;

    // clamp to max radius
    float clamped_blur = min(blur_length, MAX_BLUR_RADIUS_PIXELS);

    // normalized direction
    float2 blur_dir = velocity_pixels / (blur_length_raw + FLT_MIN);

    // accumulation with center sample
    float4 color_sum = center_color * CENTER_WEIGHT;
    float  weight_sum = CENTER_WEIGHT;

    // per-pixel noise for temporal stability (integrates with TAA)
    float jitter = (noise - 0.5f) * 0.5f;

    // sample in both directions along velocity
    [unroll]
    for (int i = 1; i <= SAMPLE_COUNT; ++i)
    {
        // normalized sample position [0, 1]
        float t = (float)i / (float)SAMPLE_COUNT;

        // apply jitter for temporal smoothing
        float t_jittered = saturate(t + jitter / (float)SAMPLE_COUNT);

        // sample distance in pixels
        float sample_dist = t_jittered * clamped_blur;

        // uv offset
        float2 offset = blur_dir * sample_dist * texel_size;

        // forward and backward sample positions
        float2 uv_fwd = uv + offset;
        float2 uv_bwd = uv - offset;

        // base falloff weight
        float falloff = sample_falloff(t);

        // forward sample
        if (is_valid_uv(uv_fwd))
        {
            float4 sample_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_fwd, 0);
            float  sample_depth = get_linear_depth(uv_fwd * get_render_uv_scale());
            float2 sample_vel   = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], uv_fwd * get_render_uv_scale(), 0).xy;
            float  sample_blur  = length(sample_vel * resolution) * shutter_ratio;

            // depth-aware weighting
            float depth_weight_fg = soft_depth_compare(center_depth, sample_depth); // sample in front
            float depth_weight_bg = soft_depth_compare(sample_depth, center_depth); // center in front

            // velocity-based weight
            float vel_weight = velocity_weight(min(sample_blur, MAX_BLUR_RADIUS_PIXELS), clamped_blur, sample_dist);

            // combine weights
            float weight = falloff * (depth_weight_fg + depth_weight_bg * vel_weight);
            weight = max(weight, 0.01f); // minimum weight to prevent harsh cutoffs

            color_sum += sample_color * weight;
            weight_sum += weight;
        }

        // backward sample
        if (is_valid_uv(uv_bwd))
        {
            float4 sample_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv_bwd, 0);
            float  sample_depth = get_linear_depth(uv_bwd * get_render_uv_scale());
            float2 sample_vel   = tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], uv_bwd * get_render_uv_scale(), 0).xy;
            float  sample_blur  = length(sample_vel * resolution) * shutter_ratio;

            // depth-aware weighting
            float depth_weight_fg = soft_depth_compare(center_depth, sample_depth);
            float depth_weight_bg = soft_depth_compare(sample_depth, center_depth);

            // velocity-based weight
            float vel_weight = velocity_weight(min(sample_blur, MAX_BLUR_RADIUS_PIXELS), clamped_blur, sample_dist);

            // combine weights
            float weight = falloff * (depth_weight_fg + depth_weight_bg * vel_weight);
            weight = max(weight, 0.01f);

            color_sum += sample_color * weight;
            weight_sum += weight;
        }
    }

    // final result
    float4 result = color_sum / weight_sum;
    result.a = center_color.a;

    return result;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get dimensions
    float2 resolution_color;
    tex.GetDimensions(resolution_color.x, resolution_color.y);

    float2 resolution_output;
    tex_uav.GetDimensions(resolution_output.x, resolution_output.y);

    // compute uv
    uint2  pixel_coord = thread_id.xy;
    float2 uv = (pixel_coord + 0.5f) / resolution_output;

    // bounds check
    if (any(pixel_coord >= uint2(resolution_output)))
        return;

    // get shutter speed from camera
    // shutter_ratio = shutter_speed / frame_time
    // - ratio < 1: fast shutter, freezes motion (less blur)
    // - ratio = 1: shutter matches frame time, standard blur
    // - ratio > 1: slow shutter, motion trails (more blur)
    float shutter_speed = pass_get_f3_value().x;
    float shutter_ratio = shutter_speed / TARGET_FRAME_TIME;

    // clamp shutter ratio to prevent excessive blur in dark scenes
    // physically, dark scenes need slower shutter for exposure, but that
    // results in unreasonably long motion trails that look bad
    shutter_ratio = clamp(shutter_ratio, 0.0f, MAX_SHUTTER_RATIO);

    // per-pixel temporal noise for TAA integration
    float noise = noise_interleaved_gradient(float2(pixel_coord), true);

    // perform blur
    float4 result = motion_blur_reconstruction(
        uv,
        float2(pixel_coord),
        resolution_color,
        shutter_ratio,
        noise
    );

    tex_uav[pixel_coord] = result;
}
