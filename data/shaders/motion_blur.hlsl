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

// spinning objects, any whose pose turned this frame (see Renderer::UpdateFrameCb_RadialBlurHubs)
static const int   SPIN_MAX_TAPS        = 64;        // along the swept arc, close up rims need them all
static const float SPIN_MAX_HALF_ANGLE  = 0.785398f; // past 45 degrees each way a streak reads the same
static const float SPIN_RIM_FRACTION    = 0.8f;      // of the radius, where open spokes give way to solid tire
static const float SPIN_TIRE_FADE       = 0.12f;     // width of that transition, fraction of the rim radius
// rubber keeps its shape and lettering: it takes color only from the slow part of its sweep and its streaks taper
static const float SPIN_TIRE_MAX_SPEED  = 9.5f;      // m/s
static const float SPIN_TIRE_TAIL_MIN   = 0.12f;
static const float SPIN_TIRE_TAIL_POWER = 0.85f;
// tread blocks stand past the tire outline by this fraction of the radius, searched for in this many steps
static const float SPIN_LUG_REACH       = 0.08f;
static const int   SPIN_LUG_STEPS       = 8;

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

/*------------------------------------------------------------------------------
    SPINNING OBJECTS
    A spinning object moves in two ways during the exposure: it turns about its axis and its pivot
    travels. A pixel is integrated by walking the surface point under it back and forth through both
    and reading what the current frame shows there. Positions are camera relative, for precision.
------------------------------------------------------------------------------*/
struct spinner
{
    float  id;         // velocity.z magnitude of its pixels
    float3 pivot;
    float3 axis;
    float  angle;      // radians this frame
    float  radius;     // 0 when unmeasured
    float  half_width;
    bool   tire;
    bool   disc;       // flat and turning about its own axis, so its spokes sweep over what shows between them
    float2 travel_uv;  // screen motion of the pivot, previous minus current
};

spinner spinner_load(uint index)
{
    float4 hub     = buffer_frame.radial_blur_hubs[index];
    float4 axis    = buffer_frame.radial_blur_axes[index];
    float4 extents = buffer_frame.radial_blur_extents[index];

    spinner s;
    s.id         = pack_material_index((uint)axis.w);
    s.pivot      = hub.xyz - get_camera_position();
    s.axis       = axis.xyz;
    s.angle      = hub.w;
    s.radius     = abs(extents.x);
    s.half_width = extents.y;
    s.tire       = extents.x < 0.0f;
    s.disc       = s.radius > 0.0f && s.half_width <= 0.75f * s.radius;
    s.travel_uv  = extents.zw;
    return s;
}

// every draw writes its motion id, the sign only says whether its material asked for radial blur
float motion_id(float2 uv)
{
    return abs(tex_velocity.SampleLevel(samplers[sampler_point_clamp], uv * get_render_uv_scale(), 0).z);
}

bool spinner_find(float id, out uint index)
{
    index = 0;
    if (id <= 0.0f)
        return false;

    uint object = unpack_material_index(id);
    for (uint i = 0; i < (uint)buffer_frame.radial_blur_hub_count; ++i)
    {
        if ((uint)buffer_frame.radial_blur_axes[i].w == object)
        {
            index = i;
            return true;
        }
    }
    return false;
}

bool spinner_covers(spinner s, float2 uv)
{
    return motion_id(uv) == s.id;
}

float depth_raw(float2 uv)
{
    return tex_depth.SampleLevel(samplers[sampler_point_clamp], uv * get_render_uv_scale(), 0).r;
}

float3 view_point(float2 uv, float depth)
{
    return get_position(depth, uv) - get_camera_position();
}

float4 view_clip(float3 position)
{
    matrix view_projection = pass_is_right_eye() ? buffer_frame.view_projection_unjittered_right : buffer_frame.view_projection_unjittered;
    return mul(float4(position + get_camera_position(), 1.0f), view_projection);
}

// view_depth is not positive behind the camera
float2 screen_point(float3 position, out float view_depth)
{
    float4 clip = view_clip(position);
    view_depth  = clip.w;
    return clip.w > 1e-6f ? ndc_to_uv(clip.xy / clip.w + buffer_frame.taa_jitter_current) : 0.0f;
}

float3 scene_point(float2 uv)
{
    return view_point(uv, depth_raw(uv));
}

// distance to what the pixel shows, the sky is simply very far
float scene_range(float2 uv)
{
    if (depth_raw(uv) <= 0.0f)
        return 1e30f;

    float range = length(scene_point(uv));
    return isfinite(range) ? range : 1e30f;
}

float3 color_at(float2 uv)
{
    return tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
}

float axle_distance(spinner s, float3 position)
{
    float3 offset = position - s.pivot;
    return length(offset - s.axis * dot(s.axis, offset));
}

// how far the exposure turns the spinner to either side, signed like its angle
float swept_half_angle(spinner s, float exposure)
{
    return sign(s.angle) * min(abs(s.angle) * exposure, SPIN_MAX_HALF_ANGLE);
}

// where a view ray crosses the camera facing side of a disc, if that is inside its rim
bool face_hit(spinner s, float3 direction, out float t)
{
    t = 0.0f;
    float facing = dot(direction, s.axis);
    if (!s.disc || abs(facing) < 0.05f)
        return false; // seen edge on there is nothing to look through

    float side  = dot(s.pivot, s.axis) <= 0.0f ? 1.0f : -1.0f;
    float plane = dot(s.pivot, s.axis) + s.half_width * side;
    t = plane / facing;
    return t > 0.0f && axle_distance(s, direction * t) < s.radius * SPIN_RIM_FRACTION;
}

// the closest a view ray comes to the axle while it is between the two sides, and where
float axle_approach(spinner s, float3 direction, out float t)
{
    t = 0.0f;
    float facing = dot(direction, s.axis);
    float along  = dot(s.pivot, s.axis);
    float t_min  = 0.0f;
    float t_max  = 1e30f;
    if (abs(facing) > 1e-4f)
    {
        float t_a = (along - s.half_width) / facing;
        float t_b = (along + s.half_width) / facing;
        t_min     = max(min(t_a, t_b), 0.0f);
        t_max     = max(t_a, t_b);
    }
    else if (abs(along) > s.half_width)
    {
        return 1e30f;
    }
    if (t_max <= t_min)
        return 1e30f;

    float3 ray_across   = direction - s.axis * facing;
    float3 pivot_across = s.pivot - s.axis * along;
    t = clamp(dot(ray_across, pivot_across) / max(dot(ray_across, ray_across), 1e-8f), t_min, t_max);
    return length(ray_across * t - pivot_across);
}

// Past the rim the only openings are between the tread blocks of the tire outline. A ray grazing the
// outline looks through one when tire shows a little further in, and the blocks sweep it at that depth.
bool lug_hit(spinner s, float3 direction, out float t)
{
    float reach   = s.radius * SPIN_LUG_REACH;
    float closest = axle_approach(s, direction, t);
    if (closest >= s.radius + reach)
        return false;

    float3 graze  = direction * t;
    float3 offset = graze - s.pivot;
    float3 inward = (s.axis * dot(s.axis, offset) - offset) * (reach / (max(closest, 1e-4f) * SPIN_LUG_STEPS));
    for (int i = 1; i <= SPIN_LUG_STEPS; ++i)
    {
        float view_depth;
        float2 uv = screen_point(graze + inward * i, view_depth);
        if (view_depth > 0.0f && is_valid_uv(uv) && spinner_covers(s, uv))
        {
            t = dot(scene_point(uv), direction);
            return true;
        }
    }
    return false;
}

// The point that turns under a spinner pixel. Inside the rim every pixel turns in the plane of the face,
// otherwise recessed details would orbit on circles of their own and their outlines would stay sharp.
float3 spinner_anchor(spinner s, float2 uv)
{
    float3 direction = normalize(view_point(uv, 0.5f));
    float t;
    return face_hit(s, direction, t) ? direction * t : scene_point(uv);
}

// The nearest disc whose spokes or tread sweep over a pixel that does not spin itself, and where they
// cross its view ray. The disc must not be hidden behind what the pixel shows.
bool spinner_in_front(float2 uv, out spinner nearest, out float3 anchor)
{
    nearest = (spinner)0;
    anchor  = 0.0f;
    float3 direction = normalize(view_point(uv, 0.5f));
    float t_nearest  = 1e30f;
    for (uint i = 0; i < (uint)buffer_frame.radial_blur_hub_count; ++i)
    {
        spinner s = spinner_load(i);
        if (!s.disc)
            continue;

        float bound = s.radius * (1.0f + SPIN_LUG_REACH) + s.half_width;
        float3 miss = cross(direction, s.pivot);
        if (dot(miss, miss) > bound * bound)
            continue;

        float t;
        if ((face_hit(s, direction, t) || lug_hit(s, direction, t)) && t < t_nearest)
        {
            t_nearest = t;
            nearest   = s;
        }
    }
    anchor = direction * t_nearest;
    return t_nearest < 1e30f && scene_range(uv) >= t_nearest - 0.02f;
}

// the circle a point turns on during the exposure, and how the travel of the spinner shifts it
struct sweep
{
    float3 center;       // on the axle
    float3 axis;
    float  half_angle;
    float3 anchor;       // the point under the pixel now
    float3 ray;          // a point on the view ray of the pixel
    float3 anchor_drift; // how far the travel moves the anchor over half the exposure
    float3 ray_drift;
};

sweep sweep_create(spinner s, float2 uv, float3 anchor, float2 resolution, float exposure)
{
    // no farther than the linear blur lets the body around the spinner travel
    float2 travel_px = -s.travel_uv * exposure * resolution;
    travel_px       *= min(1.0f, MAX_BLUR_RADIUS_PIXELS / max(length(travel_px), FLT_MIN));
    float2 uv_ahead  = uv + travel_px / resolution;
    float4 clip      = view_clip(anchor);
    float depth      = clip.z / max(clip.w, 1e-6f);

    sweep w;
    w.center       = s.pivot + s.axis * dot(s.axis, anchor - s.pivot);
    w.axis         = s.axis;
    w.half_angle   = swept_half_angle(s, exposure);
    w.anchor       = anchor;
    w.ray          = view_point(uv, 0.5f);
    w.anchor_drift = view_point(uv_ahead, depth) - view_point(uv, depth);
    w.ray_drift    = view_point(uv_ahead, 0.5f) - w.ray;
    return w;
}

// Where the point that covers the pixel at exposure time t (-1 to 1) is shown in the current frame.
// Travel is removed before the turn is, so a moving wheel keeps turning about its own axle.
float3 sweep_point(sweep w, float t)
{
    float3 position = w.anchor - w.anchor_drift * t;
    float3 ray      = w.ray - w.ray_drift * t;

    // under perspective the point stays in the plane of its circle, near edge on a constant depth stands in
    float facing = dot(ray, w.axis);
    if (facing * facing > 0.0025f * dot(ray, ray))
    {
        float distance = dot(w.center, w.axis) / facing;
        position = distance > 0.0f ? ray * distance : position;
    }

    float3 offset = position - w.center;
    float s, c;
    sincos(-t * w.half_angle, s, c);
    return w.center + offset * c + cross(w.axis, offset) * s + w.axis * dot(w.axis, offset) * (1.0f - c);
}

// on screen length of the longer half of the sweep, the turn and the travel can cancel on one side
float sweep_length_px(sweep w, float2 uv, float2 resolution)
{
    float longest = 0.0f;
    [unroll]
    for (int side = -1; side <= 1; side += 2)
    {
        float length_px = 0.0f;
        float2 previous = uv;
        [unroll]
        for (int k = 1; k <= 4; ++k)
        {
            float view_depth;
            float2 next = screen_point(sweep_point(w, side * k * 0.25f), view_depth);
            if (view_depth > 0.0f)
            {
                length_px += length((next - previous) * resolution);
                previous   = next;
            }
        }
        longest = max(longest, length_px);
    }
    return longest;
}

// 0 once the swept point went round to the far side of the spinner. Spokes passing over gaps are real,
// so only depth steps on the scale of the orbit are rejected.
float near_side(float behind, float orbit)
{
    float tolerance = 0.1f + 0.5f * orbit;
    return saturate((behind + 2.0f * tolerance) / tolerance);
}

// how well a tap tells what hides right behind the pixel, never sharper than the tap spacing
float tap_affinity(float2 tap_uv, float2 uv, float2 resolution, float spacing_sq)
{
    float2 offset = (tap_uv - uv) * resolution;
    float  w      = rcp(spacing_sq + dot(offset, offset));
    return w * w;
}

// what lies behind the swept surface, weighted by how close to the pixel each tap saw it
struct backdrop
{
    float3 color;
    float  affinity;
    float  weight;
};

// Integrates a spinner pixel, or a pixel its spokes sweep over (a gap), over the exposure. Each tap finds
// the surface point that covers the pixel at one moment and looks where it is shown now. It finds that
// point (it covers the pixel), something behind it (the pixel shows its background at that moment), or
// something in front of it like a fender or a caliper (nothing can be said and nothing bleeds in).
// clear_color is what the pixel shows while nothing of the spinner covers it.
float3 spin_blur(float2 uv, float2 resolution, float exposure, float noise, spinner s, float3 anchor, bool is_gap, float3 clear_color)
{
    sweep w = sweep_create(s, uv, anchor, resolution, exposure);
    float length_px = sweep_length_px(w, uv, resolution);
    if (length_px < MIN_BLUR_THRESHOLD)
        return clear_color;

    float3 orbit_offset = anchor - w.center;
    float  orbit        = length(orbit_offset);
    // the rim barrel, the spokes and the brake all sit at their own distance behind the anchor
    float own_behind     = scene_range(uv) - length(anchor);
    float reveal_behind  = is_gap ? -0.03f : own_behind;

    // rubber, 0 on the rim and 1 on the tread, and the part of the sweep it takes its color from
    float rubber      = 0.0f;
    float color_reach = 1.0f;
    if (s.tire && s.radius > 0.0f)
    {
        float rim   = s.radius * SPIN_RIM_FRACTION;
        rubber      = smoothstep(rim, rim * (1.0f + SPIN_TIRE_FADE), orbit);
        float speed = abs(s.angle) / max(buffer_frame.delta_time, MIN_FRAME_TIME) * orbit;
        color_reach = lerp(1.0f, min(1.0f, SPIN_TIRE_MAX_SPEED / max(speed, 1e-6f)), rubber);
    }
    // a surface closer to the axle than the view ray ever gets can never cross it
    float t_closest;
    float slack = max(orbit - axle_approach(s, normalize(w.ray), t_closest), 0.0f);

    int   taps       = clamp((int)ceil(length_px), 8, SPIN_MAX_TAPS);
    float tap_step   = rcp((float)taps);
    float spacing_sq = max(4.0f * length_px * length_px * tap_step * tap_step, 1.0f);

    float3 covered_color  = 0.0f;
    float  covered_tint   = 0.0f; // weight of the colors, the rubber takes fewer of them than it covers
    float  covered        = 0.0f;
    backdrop still        = (backdrop)0; // things that do not turn
    backdrop deeper       = (backdrop)0; // spinner surfaces further back, the brake disc behind the spokes
    for (int i = 0; i < taps; ++i)
    {
        float  t        = lerp(-1.0f, 1.0f, ((float)i + noise) * tap_step);
        float3 position = sweep_point(w, t);
        float  view_depth;
        float2 tap_uv   = screen_point(position, view_depth);
        if (view_depth <= 0.0f || !is_valid_uv(tap_uv))
            continue;

        // what the frame shows along the same view ray, distances along a ray scale with view depth
        float range     = length(position);
        float raw       = depth_raw(tap_uv);
        float scene     = raw > 0.0f ? min(range * linearize_depth(raw) / max(view_depth, 1e-6f), 1e6f) : 1e6f;
        float behind    = scene - range;
        float3 surface  = position * (scene / max(range, 1e-6f));
        float affinity  = tap_affinity(tap_uv, uv, resolution, spacing_sq);
        float r_tap     = axle_distance(s, position);
        float r_surface = axle_distance(s, surface);

        if (!spinner_covers(s, tap_uv))
        {
            // the road a tire presses into lies inside its circle, the tire yields to it
            float pressed = saturate((r_tap - r_surface) / 0.02f) * step(abs(dot(s.axis, surface - s.pivot)), s.half_width);
            float exposed = max(saturate((behind - reveal_behind) / 0.02f + 0.5f), rubber * pressed);
            still.weight += exposed;
            if (!is_gap && exposed > 0.0f)
            {
                still.color    += color_at(tap_uv) * (exposed * affinity);
                still.affinity += exposed * affinity;
            }
            continue;
        }

        // the tread between two blocks of the outline never reaches out to the view ray
        float visible  = near_side(behind, orbit);
        float coverage = visible * lerp(1.0f, saturate((r_surface - r_tap + slack) / 0.02f + 1.5f), rubber);
        still.weight  += visible - coverage;

        // a disc turning behind a caliper never covers it, behind a spoke it shows only where nothing still hides it
        float hidden  = is_gap ? 1.0f - saturate((own_behind - behind) / 0.01f) : saturate((behind - own_behind) / 0.02f - 1.0f);
        float back    = coverage * hidden;
        float tail    = lerp(SPIN_TIRE_TAIL_MIN, 1.0f, pow(saturate(1.0f - abs(t) / color_reach), SPIN_TIRE_TAIL_POWER));
        float tint    = (coverage - back) * max(abs(t) < color_reach ? lerp(1.0f, tail, rubber) : 0.0f, 1e-3f);
        float3 color  = color_at(tap_uv);
        covered_color += color * tint;
        covered_tint  += tint;
        covered       += coverage - back;
        deeper.color    += color * back;
        deeper.affinity += back * affinity;
        deeper.weight   += back;
    }

    // what shows just past the tire outline most likely hides between its tread blocks too
    if (!is_gap && rubber > 0.0f)
    {
        float3 beyond = w.center + orbit_offset * (1.0f + s.radius * SPIN_LUG_REACH / max(orbit, 1e-4f));
        float  view_depth;
        float2 beyond_uv = screen_point(beyond, view_depth);
        if (view_depth > 0.0f && is_valid_uv(beyond_uv) && !spinner_covers(s, beyond_uv) && scene_range(beyond_uv) > length(beyond))
        {
            float affinity  = tap_affinity(beyond_uv, uv, resolution, spacing_sq);
            still.color    += color_at(beyond_uv) * affinity;
            still.affinity += affinity;
        }
    }

    // a gap shows itself while uncovered, a spinner pixel whatever the taps nearest to it saw behind it
    float3 clear  = clear_color;
    float  opened = still.weight + deeper.weight;
    if (!is_gap)
    {
        float seen = still.affinity + deeper.affinity;
        float3 still_color  = still.color / max(still.affinity, 1e-30f);
        float3 deeper_color = deeper.color / max(deeper.weight, 1e-5f);
        clear  = lerp(deeper_color, still_color, still.affinity / max(seen, 1e-30f));
        opened = seen > 0.0f ? opened : 0.0f;
    }

    float total = covered + opened;
    if (total <= 1e-5f)
        return clear_color;

    return (covered_color * (covered / max(covered_tint, 1e-8f)) + clear * opened) / total;
}

// blurs along the motion vectors
float4 motion_blur_linear(
    float2 uv,
    float2 resolution,
    float  shutter_ratio,
    float  noise,
    float4 center_color,
    float4 velocity_sample
)
{
    float2 texel_size = 1.0f / resolution;
    float2 render_uv  = uv * get_render_uv_scale();
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

    // a pixel at rest keeps its own velocity below and never reads the neighborhood,
    // so one that cannot reach the blur threshold is done before the search is paid for
    if (pixel_speed <= MIN_BLUR_THRESHOLD && pixel_speed * shutter_ratio * SHUTTER_CENTER_SCALE < MIN_BLUR_THRESHOLD)
    {
        return center_color;
    }

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

// main reconstruction filter
float4 motion_blur_reconstruction(
    float2 uv,
    float2 pixel_coord,
    float2 resolution,
    float  shutter_ratio,
    float  noise
)
{
    float4 center_color    = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0);
    float4 velocity_sample = tex_velocity.SampleLevel(samplers[sampler_point_clamp], uv * get_render_uv_scale(), 0);
    float  exposure        = shutter_ratio * SHUTTER_CENTER_SCALE;

    spinner s       = (spinner)0;
    float3 anchor   = 0.0f;
    bool spinning   = false;
    bool swept      = false;
    // spinners cover a sliver of the frame, the rectangle around them spares every other pixel the search
    if (exposure > 0.0f && all(uv >= buffer_frame.radial_blur_bounds.xy) && all(uv <= buffer_frame.radial_blur_bounds.zw))
    {
        uint index;
        if (spinner_find(abs(velocity_sample.z), index))
        {
            s        = spinner_load(index);
            spinning = abs(swept_half_angle(s, exposure)) >= 1e-4f;
            anchor   = spinner_anchor(s, uv);
        }
        else
        {
            // a caliper or the background between the spokes, they pass over it during the exposure
            swept = spinner_in_front(uv, s, anchor);
        }
    }

    // mode 2: red = material asks for radial blur, green = spinning, blue = swept by a spinner in front
    if (pass_float(pass_motion_blur::mode) > 1.5f)
    {
        float4 debug_color = center_color;
        if (velocity_sample.z > 0.0f)
            debug_color.rgb = lerp(debug_color.rgb, float3(1.0f, 0.0f, 0.0f), 0.5f);
        if (spinning)
            debug_color.rgb = lerp(debug_color.rgb, float3(0.0f, 1.0f, 0.0f), 0.5f);
        if (swept)
            debug_color.rgb = lerp(debug_color.rgb, float3(0.0f, 0.3f, 1.0f), 0.6f);
        return debug_color;
    }

    if (spinning)
    {
        return float4(spin_blur(uv, resolution, exposure, noise, s, anchor, false, center_color.rgb), center_color.a);
    }

    // a swept pixel moves with the scene first, the spokes pass over that
    float4 color = motion_blur_linear(uv, resolution, shutter_ratio, noise, center_color, velocity_sample);
    if (swept)
    {
        color.rgb = spin_blur(uv, resolution, exposure, noise, s, anchor, true, color.rgb);
    }
    return color;
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
            buffer_frame.delta_time_real,
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
