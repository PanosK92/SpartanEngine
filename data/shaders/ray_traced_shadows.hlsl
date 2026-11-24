/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============
#include "common.hlsl"
#include "rt_visibility.hlsl"
//==========================

static const float SUN_ANGULAR_RADIUS = 0.00465f;

// blockers beyond this distance are treated as non occluding, prunes traversal through the far tlas
static const float SHADOW_RAY_MAX_DISTANCE = 250.0f;

struct [raypayload] ShadowPayload
{
    float hit_distance : read(caller) : write(caller, closesthit, miss);
    float shadow_alpha : read(caller) : write(caller, closesthit, miss); // how much light is blocked (0 = transparent, 1 = opaque)
};

void create_orthonormal_basis(float3 n, out float3 tangent, out float3 bitangent)
{
    float3 up = abs(n.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    tangent   = normalize(cross(up, n));
    bitangent = cross(n, tangent);
}

float3 sample_sun_direction(float3 light_dir, float2 disk_sample, float penumbra_angle)
{
    float3 tangent, bitangent;
    create_orthonormal_basis(light_dir, tangent, bitangent);
    
    float2 offset = disk_sample * penumbra_angle;
    return normalize(light_dir + tangent * offset.x + bitangent * offset.y);
}

// inline query so alpha tested foliage resolves its cutouts, the hit group below stays for the pipeline
float2 trace_opaque_shadow(float3 origin, float3 direction, float t_max)
{
    float hit_distance = rt_trace_foliage_shadow(origin, direction, t_max);
    if (hit_distance >= 0.0f)
    {
        return float2(0.0f, hit_distance);
    }

    return float2(1.0f, 0.0f);
}

// Fast hardware-only opaque shadow query for distant receivers (> 55 m) where individual leaf cutouts are sub-pixel.
float2 trace_fast_opaque_shadow(float3 origin, float3 direction, float t_max)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = 0.001f;
    ray.TMax      = max(t_max, 0.001f);
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0x01, ray);
    query.Proceed();
    if (query.CommittedStatus() != COMMITTED_NOTHING)
    {
        return float2(0.0f, query.CommittedRayT());
    }
    return float2(1.0f, 0.0f);
}

// rgb visibility through the glass in xyz, distance to the nearest glass hit in w, negative when the ray crossed none
float4 trace_glass_shadow(float visibility, float3 origin, float3 direction, float t_max)
{
    if (visibility <= 0.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, -1.0f);
    }

    RtTransmittance glass = rt_trace_transmittance(origin, direction, t_max);
    return float4(visibility * glass.color, glass.distance);
}

// an opaque blocker sizes the penumbra, a pixel dimmed only by glass uses the glass, sigma treats it as blocked
// there and carries the colour through its translucency signal, zero means nothing was hit
float shadow_blocker_distance(bool opaque_hit, float opaque_distance, float glass_distance)
{
    return opaque_hit ? opaque_distance : max(glass_distance, 0.0f);
}

void write_local_shadow(uint2 pixel, uint slice, float3 visibility, float blocker_distance)
{
    tex_uav_rt_shadows_local[uint3(pixel, slice)] = shadow_mask_pack(visibility, blocker_distance);
}

// Fixed stratified samples keep stationary shadows stable. Long showroom tubes need
// most of the samples along their long axis, while panels sample both dimensions.
// With checkerboard tracing (stride 4), 4 strata give 1 stratified sample per traced pixel
// and SIGMA's spatiotemporal filter integrates all 4 strata across neighbouring pixels.
static const uint AREA_SHADOW_SAMPLES = 4u;

float2 area_shadow_rect_uv(uint sample_index, float width, float height)
{
    uint nx = width >= height ? 4u : 2u;
    if (width >= height * 3.0f)
        nx = AREA_SHADOW_SAMPLES;
    else if (height >= width * 3.0f)
        nx = 1u;
    uint ny = AREA_SHADOW_SAMPLES / nx;
    return (float2(sample_index % nx, sample_index / nx) + 0.5f) / float2(nx, ny) - 0.5f;
}

void trace_local_light_shadow(uint2 launch_id, uint slice, LightParameters light, float3 ray_origin, float camera_distance, bool checkerboard)
{
    bool is_area = (light.flags & (1u << 6)) != 0u;
    float3 to_light = light.position - ray_origin;
    float dist_to_light = length(to_light);
    if (dist_to_light < 0.001f)
        return;

    float3 area_right = float3(0.0f, 0.0f, 0.0f);
    float3 area_up = float3(0.0f, 0.0f, 0.0f);
    if (is_area)
    {
        // Area emitters are one-sided, matching Light::compute_attenuation_area.
        if (dot(-to_light, light.direction) <= 0.0f)
            return;
        area_right = normalize(light.direction_right);
        area_up = normalize(cross(light.direction, area_right));
        float3 from_center = -to_light;
        float3 closest = light.position
            + area_right * clamp(dot(from_center, area_right), -light.area_width * 0.5f, light.area_width * 0.5f)
            + area_up * clamp(dot(from_center, area_up), -light.area_height * 0.5f, light.area_height * 0.5f);
        dist_to_light = length(closest - ray_origin);
    }
    if (dist_to_light >= light.range)
        return;

    // Stop before the fixture around the emitter, without ignoring nearby blockers
    // at the receiver (car seats, body panels and tire contacts must still occlude).
    float emitter_safety = is_area
        ? min(min(light.area_width, light.area_height) * 0.5f, 0.08f) + 0.01f
        : 0.01f;
    // Neighbouring traced pixels cover complementary strata. Use the horizontal
    // pair index so the checkerboard fill sees all four phases across its support.
    uint stride = checkerboard ? 4u : 1u;
    uint phase = ((launch_id.x >> 1u) + 2u * launch_id.y) & (stride - 1u);
    uint sample_count = is_area ? AREA_SHADOW_SAMPLES / stride : 1u;
    float visibility_sum = 0.0f;
    float hit_min = SHADOW_RAY_MAX_DISTANCE;
    bool any_hit = false;
    for (uint s = 0u; s < sample_count; s++)
    {
        float3 target = light.position;
        if (is_area)
        {
            float2 rect_uv = area_shadow_rect_uv(s * stride + phase, light.area_width, light.area_height);
            target += area_right * rect_uv.x * light.area_width + area_up * rect_uv.y * light.area_height;
        }
        float3 to_sample = target - ray_origin;
        float distance = length(to_sample);
        float2 vis = float2(1.0f, 0.0f);
        if (distance > emitter_safety + 0.001f)
        {
            float ray_max = min(distance - emitter_safety, SHADOW_RAY_MAX_DISTANCE);
            if (is_area)
            {
                // Indoor/showroom area fixtures bypass SIGMA and never need per-leaf alpha cutouts;
                // hardware-only FORCE_OPAQUE + ACCEPT_FIRST_HIT runs purely in RT Core silicon.
                vis = trace_fast_opaque_shadow(ray_origin, to_sample / distance, ray_max);
            }
            else
            {
                vis = trace_opaque_shadow(ray_origin, to_sample / distance, ray_max);
            }
        }
        visibility_sum += vis.x;
        if (vis.x < 1.0f)
        {
            hit_min = min(hit_min, vis.y);
            any_hit = true;
        }
    }
    float center_distance = length(to_light);
    float vis_avg         = visibility_sum / float(sample_count);
    float glass_t_max     = min(max(center_distance - emitter_safety, 0.001f), SHADOW_RAY_MAX_DISTANCE);
    float4 visibility     = (vis_avg > 0.0f && !is_area && camera_distance < 10.0f && center_distance < 8.0f)
        ? trace_glass_shadow(vis_avg, ray_origin, to_light / center_distance, glass_t_max)
        : float4(vis_avg.xxx, -1.0f);
    write_local_shadow(launch_id, slice, visibility.rgb, shadow_blocker_distance(any_hit, hit_min, visibility.w));
}

[shader("raygeneration")]
void ray_gen()
{
    // Checkerboard the ray tracing
    uint2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    uint2 launch_id   = DispatchRaysIndex().xy;
    bool checkerboard = any(DispatchRaysDimensions().xy < resolution);
    uint2 phase       = uint2(buffer_frame.frame & 1u, (buffer_frame.frame >> 1u) & 1u);
    uint2 pixel       = checkerboard ? launch_id * 2u + min(phase, resolution - 1u) : launch_id;
    if (any(pixel >= resolution))
    {
        return;
    }
    float2 uv = (pixel + 0.5f) / resolution;

    [unroll]
    for (uint clear_slice = 0; clear_slice < nrd_local_shadow_max; clear_slice++)
    {
        if (pass_uint(pass_ray_traced_shadows::local_shadow_lights + clear_slice) != 0u)
            write_local_shadow(pixel, clear_slice, 1.0f, 0.0f);
    }

    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
        tex_uav[pixel] = shadow_mask_pack(1.0f, 0.0f);
        return;
    }

    float3 pos_ws    = get_position(depth, render_uv_to_screen_uv(uv));
    float3 normal_ws = get_normal(uv);
    uint material_index = get_material_index(uv);
    MaterialParameters material = material_parameters[material_index];
    Surface receiver;
    receiver.flags    = material.flags;
    receiver.position = pos_ws;
    receiver.normal   = normal_ws;
    bool has_scattering = material.subsurface_scattering > 0.0f;
    bool thin_foliage = has_scattering && receiver.is_foliage();

    float3 to_camera      = get_camera_position() - pos_ws;
    float camera_distance = length(to_camera);

    float base_offset     = 0.01f + camera_distance * 0.0001f;

    // the lod error is along the drawn surface, so the offset follows its geometric normal, the visible side faces the camera
    float3 offset_normal = get_geometric_normal(uv);
    if (dot(offset_normal, offset_normal) == 0.0f)
    {
        offset_normal = normal_ws;
    }
    if (dot(offset_normal, to_camera) < 0.0f)
    {
        offset_normal = -offset_normal;
    }

    // Full-rate tracing samples both sides of the solar disk. In checkerboard
    // mode neighboring traced pixels share the pair and SIGMA filters their signals.
    {
        // slot 0 holds a local light when the world has no sun, and a sun that is off, dark or shadowless is never
        // shadowed by the lighting pass, the denoise makes the same call from the same entry
        LightParameters sun = light_parameters[0];
        bool sun_casts      = (sun.flags & (1u << 0)) != 0u && (sun.flags & (1u << 3)) != 0u && sun.intensity > 0.0f;
        float3 light_dir    = normalize(-sun.direction);
        float n_dot_l       = dot(normal_ws, light_dir);
        if (!sun_casts)
        {
            tex_uav[pixel] = shadow_mask_pack(1.0f, 0.0f);
        }
        else if (n_dot_l <= 0.0f && !has_scattering)
        {
            tex_uav[pixel] = shadow_mask_pack(0.0f, 0.0f);
        }
        else
        {
            float3 transmission = 0.0f;
            float blocker = SHADOW_RAY_MAX_DISTANCE;
            bool blocked = false;
            uint sun_samples = checkerboard ? 1u : 2u;
            [loop] for (uint i = 0; i < sun_samples; ++i)
            {
                uint disk_index = checkerboard ? (((pixel.x >> 1u) ^ pixel.y) & 1u) : i;
                float2 disk = disk_index == 0 ? float2(0.5f, 0.5f) : float2(-0.5f, -0.5f);
                float3 sample_dir = sample_sun_direction(light_dir, disk, SUN_ANGULAR_RADIUS);
                float normal_sign = thin_foliage && dot(offset_normal, sample_dir) < 0.0f ? -1.0f : 1.0f;
                float sample_offset = max(base_offset, rt_lod_shadow_offset(camera_distance));
                float3 origin = pos_ws + offset_normal * (sample_offset * normal_sign);
                float2 vis = camera_distance > 35.0f
                    ? trace_fast_opaque_shadow(origin, sample_dir, SHADOW_RAY_MAX_DISTANCE)
                    : (thin_foliage
                        ? rt_trace_canopy_transmission(origin, sample_dir, SHADOW_RAY_MAX_DISTANCE)
                        : trace_opaque_shadow(origin, sample_dir, SHADOW_RAY_MAX_DISTANCE));
                float4 glass = (vis.x > 0.0f && camera_distance < 12.0f)
                    ? trace_glass_shadow(vis.x, origin, sample_dir, min(SHADOW_RAY_MAX_DISTANCE, 12.0f))
                    : float4(vis.x.xxx, -1.0f);
                transmission += glass.rgb;
                float distance = shadow_blocker_distance(vis.x < 0.99f, vis.y, glass.w);
                if (distance > 0.0f)
                {
                    blocker = min(blocker, distance);
                    blocked = true;
                }
            }
            tex_uav[pixel] = shadow_mask_pack(transmission / sun_samples, blocked ? blocker : 0.0f);
        }
    }

    // The lighting pass samples these slots instead of the shadow atlas. Every
    // assigned local light must write its own visibility, even with no active sun.
    [unroll] for (uint slot = 0u; slot < nrd_local_shadow_max; slot++)
    {
        uint light_i = pass_uint(pass_ray_traced_shadows::local_shadow_lights + slot);
        if (light_i == 0u || light_i >= buffer_frame.cluster_light_count)
            continue;
        LightParameters light = light_parameters[light_i];
        // No reflected light reaches a receiver facing away from every point
        // on the emitter. Keep transmission receivers and straddling rectangles
        // on the full path; their back lighting still needs shadow visibility.
        if (pass_bool(pass_ray_traced_shadows::light_culling) && !has_scattering)
        {
            float facing = dot(normal_ws, light.position - pos_ws);
            if ((light.flags & (1u << 6)) != 0u)
            {
                float3 right = normalize(light.direction_right);
                float3 up = normalize(cross(light.direction, right));
                facing += 0.5f * (light.area_width * abs(dot(normal_ws, right))
                               + light.area_height * abs(dot(normal_ws, up)));
            }
            if (facing < -0.001f) continue;
        }
        // the lighting pass drops a light whose exposed energy at this pixel is sub perceptual and never reads its shadow,
        // headlights in daylight reach a few tens of meters of a 300 m range, the rest of the screen needs no ray
        Light reach;
        reach.Build(light_i, receiver);
        if (!reach.can_contribute())
            continue;
        float3 to_light    = normalize(light.position - pos_ws);
        float local_offset = max(0.001f + min(camera_distance * 0.00001f, 0.002f), rt_lod_shadow_offset(camera_distance));
        float normal_sign  = thin_foliage && dot(offset_normal, to_light) < 0.0f ? -1.0f : 1.0f;
        trace_local_light_shadow(pixel, slot, light, pos_ws + offset_normal * (local_offset * normal_sign), camera_distance, checkerboard);
    }
}

[shader("miss")]
void miss(inout ShadowPayload payload : SV_RayPayload)
{
    payload.hit_distance = -1.0f;
    payload.shadow_alpha = 0.0f;
}

[shader("closesthit")]
void closest_hit(inout ShadowPayload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    payload.hit_distance = RayTCurrent();
    payload.shadow_alpha = 1.0f;
}
