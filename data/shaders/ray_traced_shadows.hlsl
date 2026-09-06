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

//= INCLUDES =========
#include "common.hlsl"
//====================

static const float SUN_ANGULAR_RADIUS = 0.00465f;

// blockers beyond this distance are treated as non occluding, prunes traversal through the far tlas
static const float SHADOW_RAY_MAX_DISTANCE = 1000.0f;

struct [raypayload] ShadowPayload
{
    float hit_distance : read(caller) : write(caller, closesthit, miss);
    float shadow_alpha : read(caller) : write(caller, closesthit, miss); // how much light is blocked (0 = transparent, 1 = opaque)
};

float2 concentric_disk_sample(float2 u)
{
    float2 offset = 2.0f * u - 1.0f;
    
    if (offset.x == 0.0f && offset.y == 0.0f)
        return float2(0.0f, 0.0f);
    
    float theta, r;
    if (abs(offset.x) > abs(offset.y))
    {
        r     = offset.x;
        theta = PI * 0.25f * (offset.y / offset.x);
    }
    else
    {
        r     = offset.y;
        theta = PI * 0.5f - PI * 0.25f * (offset.x / offset.y);
    }
    
    return float2(cos(theta), sin(theta)) * r;
}

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

float2 trace_opaque_shadow(float3 origin, float3 direction, float t_max)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = 0.001f;
    ray.TMax      = max(t_max, 0.001f);

    ShadowPayload payload;
    payload.hit_distance = -1.0f;
    payload.shadow_alpha = 0.0f;

    // opaque instances only, glass is 0x02 and a layered walk against a car tlas tdrs on play
    TraceRay(
        tlas,
        RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0x01,
        0,
        1,
        0,
        ray,
        payload
    );

    float local_hit_distance = payload.hit_distance;
    float local_shadow_alpha = payload.shadow_alpha;
    if (local_hit_distance >= 0.0f)
    {
        return float2(saturate(1.0f - local_shadow_alpha), local_hit_distance);
    }

    return float2(1.0f, 0.0f);
}

void write_local_shadow(uint2 launch_id, uint slice, float visibility, float hit_dist, float dist_to_light, float light_size)
{
    tex_uav_rt_shadows_local[uint3(launch_id, slice)] = float4(
        visibility,
        hit_dist,
        dist_to_light,
        light_size
    );
}

// Fixed stratified samples keep stationary shadows stable. Long showroom tubes need
// most of the samples along their long axis, while panels sample both dimensions.
static const uint AREA_SHADOW_SAMPLES = 8u;

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

void trace_local_light_shadow(uint2 launch_id, uint slice, LightParameters light, float3 ray_origin)
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
    uint sample_count = is_area ? AREA_SHADOW_SAMPLES : 1u;
    float visibility_sum = 0.0f;
    float hit_min = SHADOW_RAY_MAX_DISTANCE;
    bool any_hit = false;
    for (uint s = 0u; s < sample_count; s++)
    {
        float3 target = light.position;
        if (is_area)
        {
            float2 rect_uv = area_shadow_rect_uv(s, light.area_width, light.area_height);
            target += area_right * rect_uv.x * light.area_width + area_up * rect_uv.y * light.area_height;
        }
        float3 to_sample = target - ray_origin;
        float distance = length(to_sample);
        float2 vis = float2(1.0f, 0.0f);
        if (distance > emitter_safety + 0.001f)
        {
            vis = trace_opaque_shadow(ray_origin, to_sample / distance,
                min(distance - emitter_safety, SHADOW_RAY_MAX_DISTANCE));
        }
        visibility_sum += vis.x;
        if (vis.x < 1.0f)
        {
            hit_min = min(hit_min, vis.y);
            any_hit = true;
        }
    }
    write_local_shadow(launch_id, slice, visibility_sum / float(sample_count),
        any_hit ? hit_min : 0.0f, dist_to_light, is_area ? max(light.area_width, light.area_height) : 0.1f);
}

[shader("raygeneration")]
void ray_gen()
{
    uint2 launch_id   = DispatchRaysIndex().xy;
    uint2 launch_size = DispatchRaysDimensions().xy;
    float2 uv         = (launch_id + 0.5f) / launch_size;

    [unroll]
    for (uint clear_slice = 0; clear_slice < nrd_local_shadow_max; clear_slice++)
    {
        write_local_shadow(launch_id, clear_slice, 1.0f, 0.0f, 1.0f, 1.0f);
    }

    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
        tex_uav[launch_id] = float4(1.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);

    float camera_distance = length(get_camera_position() - pos_ws);
    float base_offset     = 0.01f + camera_distance * 0.0001f;
    float3 ray_origin     = pos_ws + normal_ws * base_offset;

    float  frame_index = (float)buffer_frame.frame;
    float2 xi_base;
    xi_base.x = frac(hash(float2(launch_id))         + frame_index * 0.7548776662f);
    xi_base.y = frac(hash(float2(launch_id) + 31.7f) + frame_index * 0.5698402909f);

    // directional sun, 1 spp jittered disk, sigma reconstructs the penumbra
    {
        float3 light_dir = normalize(-light_parameters[0].direction);
        float n_dot_l    = dot(normal_ws, light_dir);
        if (n_dot_l <= 0.0f)
        {
            tex_uav[launch_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        else
        {
            float2 disk       = concentric_disk_sample(xi_base);
            float3 sample_dir = sample_sun_direction(light_dir, disk, SUN_ANGULAR_RADIUS);
            float2 vis        = trace_opaque_shadow(ray_origin, sample_dir, SHADOW_RAY_MAX_DISTANCE);
            float visibility  = vis.x;
            float hit_dist    = vis.x < 1.0f ? vis.y : 0.0f;
            tex_uav[launch_id] = float4(visibility, hit_dist, 0.0f, 1.0f);
        }
    }

    // The lighting pass samples these slots instead of the shadow atlas. Every
    // assigned local light must write its own visibility, even with no active sun.
    for (uint light_i = 1u; light_i < buffer_frame.cluster_light_count; light_i++)
    {
        LightParameters light = light_parameters[light_i];
        uint slot = (light.flags >> 8u) & 7u;
        if (slot == 0u || slot > nrd_local_shadow_max || (light.flags & (1u << 3)) == 0u)
            continue;
        float local_offset = 0.001f + min(camera_distance * 0.00001f, 0.002f);
        trace_local_light_shadow(launch_id, slot - 1u, light, pos_ws + normal_ws * local_offset);
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
