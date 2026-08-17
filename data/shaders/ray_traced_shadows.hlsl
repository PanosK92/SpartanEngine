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

    TraceRay(
        tlas,
        RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF,
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

    bool scene_has_transparents = pass_get_f3_value().x != 0.0f;

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

            float accumulated_alpha = 0.0f;
            float first_hit_dist    = -1.0f;

            if (!scene_has_transparents)
            {
                float2 vis = trace_opaque_shadow(ray_origin, sample_dir, SHADOW_RAY_MAX_DISTANCE);
                accumulated_alpha = 1.0f - vis.x;
                first_hit_dist    = vis.y > 0.0f ? vis.y : -1.0f;
            }
            else
            {
                float3 current_origin = ray_origin;
                for (uint layer = 0; layer < 4; layer++)
                {
                    RayDesc ray;
                    ray.Origin    = current_origin;
                    ray.Direction = sample_dir;
                    ray.TMin      = 0.001f;
                    ray.TMax      = SHADOW_RAY_MAX_DISTANCE;

                    ShadowPayload payload;
                    payload.hit_distance = -1.0f;
                    payload.shadow_alpha = 0.0f;

                    TraceRay(tlas, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 1, 0, ray, payload);

                    float local_hit_distance = payload.hit_distance;
                    float local_shadow_alpha = payload.shadow_alpha;
                    if (local_hit_distance < 0.0f)
                    {
                        break;
                    }

                    if (first_hit_dist < 0.0f)
                    {
                        first_hit_dist = local_hit_distance;
                    }

                    if (local_shadow_alpha >= 1.0f)
                    {
                        accumulated_alpha = 1.0f;
                        break;
                    }

                    accumulated_alpha = 1.0f - (1.0f - accumulated_alpha) * (1.0f - local_shadow_alpha);
                    if (accumulated_alpha >= 0.99f)
                    {
                        accumulated_alpha = 1.0f;
                        break;
                    }

                    current_origin = current_origin + sample_dir * (local_hit_distance + 0.01f);
                }
            }

            float visibility = 1.0f - accumulated_alpha;
            float hit_dist   = (first_hit_dist > 0.0f && accumulated_alpha > 0.0f) ? first_hit_dist : 0.0f;
            tex_uav[launch_id] = float4(visibility, hit_dist, 0.0f, 1.0f);
        }
    }

    // local lights, one deterministic ray to the emitter center, sigma reconstructs the penumbra
    // from distance_to_occluder and light_size, a jittered target would feed sigma a stochastic
    // signal it cannot resolve and the leftover coin flip survives as salt and pepper
    uint light_count = buffer_frame.cluster_light_count;
    for (uint light_i = 1; light_i < light_count; light_i++)
    {
        LightParameters light = light_parameters[light_i];
        uint slot = (light.flags >> 8u) & 7u;
        if (slot == 0u)
        {
            continue;
        }

        uint slice = slot - 1u;
        bool is_area = (light.flags & uint(1u << 6)) != 0u;

        float3 target        = light.position.xyz;
        float  light_size    = 0.1f;
        float  emitter_safety = 0.01f;
        if (is_area)
        {
            // geometric mean is the side of the square with the same emitter area, it keeps a thin
            // strip from reading as a 4 metre wide source while still softening well
            light_size     = sqrt(max(light.area_width * light.area_height, 1e-6f));
            emitter_safety = min(min(light.area_width, light.area_height) * 0.5f, 0.08f) + 0.01f;
        }

        float3 to_light = target - ray_origin;
        float  dist     = length(to_light);
        if (dist < 0.0001f)
        {
            write_local_shadow(launch_id, slice, 1.0f, 0.0f, 0.001f, light_size);
            continue;
        }

        float3 direction = to_light / dist;
        // below horizon is a lighting term, packing it as a zero-t occluder leaves hard dots
        if (dot(normal_ws, direction) <= 0.0f)
        {
            write_local_shadow(launch_id, slice, 1.0f, 0.0f, dist, light_size);
            continue;
        }

        float t_max    = max(dist - emitter_safety, 0.001f);
        float2 vis     = trace_opaque_shadow(ray_origin, direction, t_max);
        float hit_dist = vis.x < 0.5f ? vis.y : 0.0f;
        const float k_self_hit = 0.08f;
        if (hit_dist > 0.0f && hit_dist < k_self_hit)
        {
            vis.x    = 1.0f;
            hit_dist = 0.0f;
        }
        write_local_shadow(launch_id, slice, vis.x, hit_dist, dist, light_size);
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
    uint material_index     = InstanceID();
    MaterialParameters mat  = material_parameters[material_index];
    
    // transparent materials cast partial shadows based on their opacity
    payload.hit_distance = RayTCurrent();
    payload.shadow_alpha = mat.color.a;
}
