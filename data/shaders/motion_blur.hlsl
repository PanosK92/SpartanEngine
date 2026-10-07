/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= includes =========
#include "common.hlsl"
//====================

// tuning parameters
static const int   SAMPLE_COUNT             = 7;     // samples per direction (total = 2 * count + 1 = 15)
static const float MAX_BLUR_RADIUS_PIXELS   = 48.0f; // maximum blur extent in pixels
static const float MIN_BLUR_THRESHOLD       = 0.75f; // minimum velocity in pixels to trigger blur
static const float DEPTH_SCALE              = 50.0f; // depth comparison sensitivity
static const float CENTER_WEIGHT            = 1.5f;  // weight for center sample (higher = sharper center)
static const float MAX_SHUTTER_RATIO        = 8.0f;
static const float MIN_FRAME_TIME           = 1.0f / 1000.0f;
static const float SHUTTER_CENTER_SCALE     = 0.5f;

// neighborhood max search radius in pixels, used to stabilize blur length
// without snapping to a fixed tile grid (which produces visible square artifacts)
static const float NEIGHBORHOOD_RADIUS_PIXELS = 16.0f;

// radial motion blur, adaptive sampling bounds
static const int MAX_RADIAL_SAMPLES = 128;
static const float MAX_RADIAL_ANGLE = 3.14159265f; // a full revolution over the centered exposure

// soft depth comparison, linear depth larger is farther, returns 1 when depth_a is closer or equal
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

float2 velocity_ndc_to_uv(float2 velocity)
{
    return velocity * float2(0.5f, -0.5f);
}

float get_linear_depth_point(float2 uv)
{
    float depth = tex_depth.SampleLevel(
        samplers[sampler_point_clamp],
        uv,
        0
    ).r;
    return linearize_depth(depth);
}

float velocity_similarity(float2 velocity_a, float2 velocity_b)
{
    float speed_a = length(velocity_a);
    float speed_b = length(velocity_b);

    if (max(speed_a, speed_b) < MIN_BLUR_THRESHOLD)
    {
        return 1.0f;
    }

    float direction_similarity = saturate(
        dot(
            velocity_a / (speed_a + FLT_MIN),
            velocity_b / (speed_b + FLT_MIN)
        )
    );
    float speed_similarity =
        min(speed_a, speed_b) /
        (max(speed_a, speed_b) + FLT_MIN);

    return
        direction_similarity *
        smoothstep(0.25f, 0.75f, speed_similarity);
}

float reconstruction_weight(
    float center_depth,
    float sample_depth,
    float2 center_velocity_pixels,
    float2 sample_velocity_pixels,
    float sample_distance,
    float center_blur,
    float shutter_ratio
)
{
    float exposure_scale =
        shutter_ratio *
        SHUTTER_CENTER_SCALE;
    float2 center_motion =
        center_velocity_pixels *
        exposure_scale;
    float2 sample_motion =
        sample_velocity_pixels *
        exposure_scale;

    float depth_similarity =
        soft_depth_compare(center_depth, sample_depth) *
        soft_depth_compare(sample_depth, center_depth);
    float same_surface =
        depth_similarity *
        velocity_similarity(
            center_motion,
            sample_motion
        );

    float sample_is_closer = saturate(
        (center_depth - sample_depth) *
        DEPTH_SCALE
    );
    float sample_speed = length(sample_motion);
    float direction_coverage = 0.0f;
    if (sample_speed > MIN_BLUR_THRESHOLD)
    {
        float2 sample_direction =
            sample_motion /
            sample_speed;
        float2 center_direction =
            center_motion /
            (length(center_motion) + FLT_MIN);
        float direction_alignment =
            abs(dot(sample_direction, center_direction));
        direction_coverage =
            direction_alignment *
            direction_alignment;
    }

    float sample_blur =
        min(
            sample_speed,
            MAX_BLUR_RADIUS_PIXELS
        );
    float foreground_coverage =
        sample_is_closer *
        direction_coverage *
        velocity_weight(
            sample_blur,
            center_blur,
            sample_distance
        );

    return saturate(same_surface + foreground_coverage);
}

// find the dominant velocity in a neighborhood centered on this pixel
// the search is anchored to the pixel position rather than a snapped tile grid,
// so adjacent pixels see almost the same neighborhood and the result varies
// smoothly across the screen instead of producing visible square tile boundaries
float2 get_neighborhood_max_velocity(
    float2 uv,
    float2 texel_size,
    float2 resolution,
    float center_depth,
    float jitter,
    float center_mask
)
{
    float2 search_step = NEIGHBORHOOD_RADIUS_PIXELS * texel_size;

    // small per-pixel rotation of the sample pattern breaks any residual grid
    // alignment without changing the pattern from frame to frame
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
            float2 offset    = mul(rot, float2(x, y) * search_step);
            float2 sample_uv = uv + offset;
            float2 render_uv =
                sample_uv *
                get_render_uv_scale();
            float2 sample_vel = tex_velocity.SampleLevel(
                samplers[sampler_point_clamp],
                render_uv,
                0
            ).xy;
            float sample_depth =
                get_linear_depth_point(render_uv);
            float depth_similarity =
                soft_depth_compare(center_depth, sample_depth) *
                soft_depth_compare(sample_depth, center_depth);
            float sample_len =
                length(
                    velocity_ndc_to_uv(sample_vel) *
                    resolution
                );

            if (
                tex_velocity.SampleLevel(samplers[sampler_point_clamp], render_uv, 0).z == center_mask &&
                depth_similarity > 0.5f &&
                sample_len > best_length
            )
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

// Object identity selects the rotation record, never screen-space proximity.
bool find_radial_hub(float mask, out uint index)
{
    index = 0;
    if (mask <= 0.0f)
        return false;
    uint id = unpack_material_index(mask);
    for (uint i = 0; i < (uint)buffer_frame.radial_blur_hub_count; ++i)
    {
        if ((uint)buffer_frame.radial_blur_axes[i].w == id)
        {
            index = i;
            return true;
        }
    }
    return false;
}

// One exact wheel stencil value is shared by tire, rim and brake interior.
// Different wheel/scene mask values are always excluded.
bool radial_mask_matches(float mask, uint index)
{
    return mask > 0.0f && mask == pack_material_index((uint)buffer_frame.radial_blur_axes[index].w);
}

// Clip every bilinear footprint tap against the wheel mask, so even subpixel
// boundary samples cannot bring in asphalt, sky or body paint.
float radial_masked_color(float2 uv, float2 resolution, uint index, out float4 color)
{
    float2 pixel = uv * resolution - 0.5f;
    float2 base = floor(pixel);
    float2 f = frac(pixel);
    color = 0.0f;
    float coverage = 0.0f;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            float2 tap_uv = (base + float2(x, y) + 0.5f) / resolution;
            if (!is_valid_uv(tap_uv))
                continue;
            float mask = tex_velocity.SampleLevel(samplers[sampler_point_clamp], tap_uv * get_render_uv_scale(), 0).z;
            if (!radial_mask_matches(mask, index))
                continue;
            float weight = (x == 0 ? 1.0f - f.x : f.x) * (y == 0 ? 1.0f - f.y : f.y);
            color += tex.SampleLevel(samplers[sampler_point_clamp], tap_uv, 0) * weight;
            coverage += weight;
        }
    }
    color /= max(coverage, FLT_MIN);
    return coverage;
}

// Validate every color footprint tap, not just its center: bilinear filtering otherwise
// leaks ground/body colors through an accepted wheel pixel at silhouettes.
float masked_color(float2 uv, float2 resolution, float mask, out float4 color)
{
    float2 pixel = uv * resolution - 0.5f;
    float2 base = floor(pixel);
    float2 f = frac(pixel);
    color = 0.0f;
    float coverage = 0.0f;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            float2 tap_uv = (base + float2(x, y) + 0.5f) / resolution;
            if (!is_valid_uv(tap_uv))
                continue;
            float tap_mask = tex_velocity.SampleLevel(samplers[sampler_point_clamp], tap_uv * get_render_uv_scale(), 0).z;
            if (tap_mask != mask)
                continue;
            float weight = (x == 0 ? 1.0f - f.x : f.x) * (y == 0 ? 1.0f - f.y : f.y);
            color += tex.SampleLevel(samplers[sampler_point_clamp], tap_uv, 0) * weight;
            coverage += weight;
        }
    }
    color /= max(coverage, FLT_MIN);
    return coverage;
}

float3 radial_scene_position(float2 uv)
{
    float depth = tex_depth.SampleLevel(samplers[sampler_point_clamp], uv * get_render_uv_scale(), 0).r;
    return get_position(depth, uv);
}

// The actual surface point, so tread, sidewall and rim each orbit the axis at their own
// radius and axial offset. A single hub plane would map the tread/sidewall to the wrong
// radius whenever the wheel is seen at an angle, turning the circles into offset ellipses.
bool radial_surface_position(float2 uv, out float3 position)
{
    float depth = tex_depth.SampleLevel(samplers[sampler_point_clamp], uv * get_render_uv_scale(), 0).r;
    position = get_position(depth, uv);
    return depth > 0.0f && all(isfinite(position));
}

// Rotate the point rigidly about the wheel axis line, then project it for the current eye.
// Translation or camera extrapolation here would turn the closed ring into a spiral.
bool radial_sample_uv(float3 position, uint index, float angle, out float2 uv, out float3 sample_position)
{
    float3 pivot = buffer_frame.radial_blur_hubs[index].xyz;
    float3 axis = buffer_frame.radial_blur_axes[index].xyz;
    float3 d = position - pivot;
    float cs, sn;
    sincos(angle, sn, cs);
    float3 rotated = d * cs + cross(axis, d) * sn + axis * dot(axis, d) * (1.0f - cs);
    sample_position = pivot + rotated;
    matrix vp = pass_is_right_eye() ? buffer_frame.view_projection_unjittered_right : buffer_frame.view_projection_unjittered;
    float4 clip = mul(float4(sample_position, 1.0f), vp);
    uv = 0.0f;
    if (clip.w <= 0.0001f)
    {
        return false;
    }
    uv = ndc_to_uv(clip.xy / clip.w + buffer_frame.taa_jitter_current);
    return is_valid_uv(uv);
}

bool radial_sample_uv(float3 position, uint index, float angle, out float2 uv)
{
    float3 sample_position;
    return radial_sample_uv(position, index, angle, uv, sample_position);
}

// 0 when the rotated point went round to the hidden back of the tire, 1 otherwise.
// Spokes sweeping over brake gaps (and gaps behind spokes) are both what the eye sees
// during the exposure, so only depth steps comparable to the orbit radius are rejected.
float radial_visibility(float2 sample_uv, float3 sample_position, float orbit_radius)
{
    float3 camera = get_camera_position();
    float expected = length(sample_position - camera);
    float visible = length(radial_scene_position(sample_uv) - camera);
    float tolerance = 0.1f + 0.5f * orbit_radius;
    return saturate((visible - expected + 2.0f * tolerance) / tolerance);
}

float4 motion_blur_radial_reconstruction(
    float2 uv, float2 resolution, float shutter_ratio, float noise,
    float4 center_color, float3 position, uint index)
{
    float exposure = shutter_ratio * SHUTTER_CENTER_SCALE;
    if (exposure <= 0.0f)
        return center_color;
    float frame_angle = buffer_frame.radial_blur_hubs[index].w;
    // Spins exceeding one turn expose the whole circumference; avoid repeated undersampled turns.
    float angle = min(frame_angle * exposure, MAX_RADIAL_ANGLE);
    float2 end_uv;
    float path_pixels = 0.0f;
    // Arc-length estimate at quarter intervals also catches rotations with coincident endpoints.
    float2 previous_uv = uv;
    for (int k = 1; k <= 4; ++k)
    {
        if (radial_sample_uv(position, index, angle * (float)k * 0.25f, end_uv))
        {
            path_pixels += length((end_uv - previous_uv) * resolution);
            previous_uv = end_uv;
        }
    }
    if (path_pixels < MIN_BLUR_THRESHOLD)
        return center_color;
    // Box-filtered shutter integral along the arc actually swept, centered on the current pose.
    // One sample per arc pixel keeps the rings continuous; the full turn is reached only at high spin.
    int count = clamp((int)ceil(2.0f * path_pixels), 8, MAX_RADIAL_SAMPLES);
    float3 axis = buffer_frame.radial_blur_axes[index].xyz;
    float3 offset = position - buffer_frame.radial_blur_hubs[index].xyz;
    float orbit_radius = length(offset - axis * dot(axis, offset));
    float4 sum = 0.0f;
    float weights = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        float t = ((float)i + noise) / (float)count * 2.0f - 1.0f;
        float2 sample_uv;
        float3 sample_position;
        if (!radial_sample_uv(position, index, t * angle, sample_uv, sample_position))
            continue;
        float sample_mask = tex_velocity.SampleLevel(samplers[sampler_point_clamp], sample_uv * get_render_uv_scale(), 0).z;
        if (!radial_mask_matches(sample_mask, index))
            continue;
        float4 color;
        float coverage = radial_masked_color(sample_uv, resolution, index, color) * radial_visibility(sample_uv, sample_position, orbit_radius);
        sum += color * coverage;
        weights += coverage;
    }
    float4 result = weights > FLT_MIN ? sum / weights : center_color;
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
    float4 velocity_sample = tex_velocity.SampleLevel(samplers[sampler_point_clamp], render_uv, 0);
    bool is_radial = velocity_sample.z > 0.0f;

    uint radial_index;
    float3 radial_position;
    bool radial_domain = false;
    if (is_radial && find_radial_hub(velocity_sample.z, radial_index))
    {
        radial_domain = radial_surface_position(uv, radial_position);
    }
    // A non-radial destination never enters the radial pass, regardless of hub proximity.

    // Mode 2: red = radial geometry, green = geometry with a matching rotation record.
    if (pass_float(pass_motion_blur::mode) > 1.5f)
    {
        float4 debug_color = center_color;
        if (is_radial)
            debug_color.rgb = lerp(debug_color.rgb, float3(1.0f, 0.0f, 0.0f), 0.5f);
        if (radial_domain)
            debug_color.rgb = lerp(debug_color.rgb, float3(0.0f, 1.0f, 0.0f), 0.5f);
        return debug_color;
    }

    if (radial_domain)
        return motion_blur_radial_reconstruction(uv, resolution, shutter_ratio, noise, center_color, radial_position, radial_index);

    float center_depth = get_linear_depth_point(render_uv);
    float raw_depth = get_depth(render_uv);
    float2 pixel_velocity = velocity_sample.xy;
    if (raw_depth < 0.0001f && dot(pixel_velocity, pixel_velocity) < 1e-12f)
        pixel_velocity = compute_sky_velocity(uv);

    float2 pixel_velocity_uv =
        velocity_ndc_to_uv(pixel_velocity);
    float2 pixel_velocity_pixels =
        pixel_velocity_uv *
        resolution;
    float pixel_speed =
        length(pixel_velocity_pixels);

    // neighborhood dominant velocity, sampled around the pixel itself rather than
    // snapped to a fixed tile grid, which avoids visible square boundaries
    float2 neighborhood_velocity =
        get_neighborhood_max_velocity(
            uv,
            texel_size,
            resolution,
            center_depth,
            noise,
            velocity_sample.z
        );
    float2 neighborhood_velocity_uv =
        velocity_ndc_to_uv(neighborhood_velocity);
    float2 neighborhood_velocity_pixels =
        neighborhood_velocity_uv *
        resolution;
    float neighborhood_speed =
        length(neighborhood_velocity_pixels);

    // use the pixel's own direction but blend the magnitude toward the neighborhood max
    // the boost is gated by similarity to prevent silhouette pixels next to fast-moving
    // backgrounds from inheriting the background's blur length and smearing across it
    float2 velocity;
    if (pixel_speed > MIN_BLUR_THRESHOLD)
    {
        float2 pixel_dir =
            pixel_velocity_pixels /
            (pixel_speed + FLT_MIN);
        float2 neigh_dir =
            neighborhood_velocity_pixels /
            (neighborhood_speed + FLT_MIN);

        // motion is coherent when this pixel and its neighborhood agree on direction
        // and have comparable speeds, opposite directions cannot stabilize each other
        float dir_agreement = saturate(dot(pixel_dir, neigh_dir));
        float speed_ratio   = pixel_speed / (neighborhood_speed + FLT_MIN);
        float coherence     = dir_agreement * smoothstep(0.5f, 1.0f, speed_ratio);

        float stable_speed = lerp(pixel_speed, neighborhood_speed, 0.7f * coherence);
        velocity = pixel_dir * stable_speed;
    }
    else
    {
        velocity = pixel_velocity_pixels;
    }

    float2 velocity_pixels = velocity;
    float  blur_length_raw = length(velocity_pixels);

    // apply shutter ratio
    float blur_length =
        blur_length_raw *
        shutter_ratio *
        SHUTTER_CENTER_SCALE;

    // exit if blur became too small after adjustments
    if (blur_length < MIN_BLUR_THRESHOLD)
    {
        return center_color;
    }

    // clamp to max radius
    float clamped_blur = min(blur_length, MAX_BLUR_RADIUS_PIXELS);

    // normalized direction
    float2 blur_dir = velocity_pixels / (blur_length_raw + FLT_MIN);

    // accumulation with center sample
    float4 color_sum = center_color * CENTER_WEIGHT;
    float  weight_sum = CENTER_WEIGHT;

    // stable per-pixel stratification for the reconstruction samples
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
            float4 sample_color;
            float mask_coverage = masked_color(uv_fwd, resolution, velocity_sample.z, sample_color);
            float2 sample_render_uv =
                uv_fwd *
                get_render_uv_scale();
            float sample_depth =
                get_linear_depth_point(sample_render_uv);
            float2 sample_velocity =
                velocity_ndc_to_uv(
                    tex_velocity.SampleLevel(
                        samplers[sampler_point_clamp],
                        sample_render_uv,
                        0
                    ).xy
                ) *
                resolution;
            float weight =
                falloff * mask_coverage *
                reconstruction_weight(
                    center_depth,
                    sample_depth,
                    velocity_pixels,
                    sample_velocity,
                    sample_dist,
                    clamped_blur,
                    shutter_ratio
                );

            color_sum  += sample_color * weight;
            weight_sum += weight;
        }

        // backward sample
        if (is_valid_uv(uv_bwd))
        {
            float4 sample_color;
            float mask_coverage = masked_color(uv_bwd, resolution, velocity_sample.z, sample_color);
            float2 sample_render_uv =
                uv_bwd *
                get_render_uv_scale();
            float sample_depth =
                get_linear_depth_point(sample_render_uv);
            float2 sample_velocity =
                velocity_ndc_to_uv(
                    tex_velocity.SampleLevel(
                        samplers[sampler_point_clamp],
                        sample_render_uv,
                        0
                    ).xy
                ) *
                resolution;
            float weight =
                falloff * mask_coverage *
                reconstruction_weight(
                    center_depth,
                    sample_depth,
                    velocity_pixels,
                    sample_velocity,
                    sample_dist,
                    clamped_blur,
                    shutter_ratio
                );

            color_sum  += sample_color * weight;
            weight_sum += weight;
        }
    }

    // final result
    float4 result = color_sum / max(weight_sum, FLT_MIN);
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
    {
        return;
    }

    float shutter_speed = pass_float(pass_motion_blur::shutter_speed);
    float frame_time =
        max(
            buffer_frame.delta_time,
            MIN_FRAME_TIME
        );
    float shutter_ratio =
        clamp(
            shutter_speed /
            frame_time,
            0.0f,
            MAX_SHUTTER_RATIO
        );

    // This pass follows temporal upscaling, so animated noise would flicker without accumulation.
    float noise = noise_interleaved_gradient(float2(pixel_coord), false);

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
