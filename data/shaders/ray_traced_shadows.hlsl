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

[shader("raygeneration")]
void ray_gen()
{
    uint2 launch_id   = DispatchRaysIndex().xy;
    uint2 launch_size = DispatchRaysDimensions().xy;
    float2 uv         = (launch_id + 0.5f) / launch_size;
    
    // early out for sky pixels, fully lit with no blocker
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
        tex_uav[launch_id] = float4(1.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    float3 light_dir = normalize(-light_parameters[0].direction);
    
    // backface culling, fully shadowed with no blocker distance
    float n_dot_l = dot(normal_ws, light_dir);
    if (n_dot_l <= 0.0f)
    {
        tex_uav[launch_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    
    // ray origin with normal offset
    float camera_distance = length(get_camera_position() - pos_ws);
    float base_offset     = 0.01f + camera_distance * 0.0001f;
    float3 ray_origin     = pos_ws + normal_ws * base_offset;
    
    // one stochastic sample per frame toward the jittered sun disk, the denoiser accumulates
    // the temporal history so a single ray converges to a soft penumbra over a few frames
    static const uint SHADOW_SAMPLES         = 1;
    static const uint MAX_TRANSPARENT_LAYERS = 4;
    
    // per pixel per frame low discrepancy sample, r2 frame rotation for the denoiser to accumulate
    float  frame_index = (float)buffer_frame.frame;
    float2 xi_base;
    xi_base.x = frac(hash(float2(launch_id))         + frame_index * 0.7548776662f);
    xi_base.y = frac(hash(float2(launch_id) + 31.7f) + frame_index * 0.5698402909f);
    
    float sum_visibility = 0.0f;
    float sum_blocker    = 0.0f;
    float blocker_weight = 0.0f;
    
    // cpu sets x to 1 when any transparent material exists in the draw list
    bool scene_has_transparents = pass_get_f3_value().x != 0.0f;
    
    for (uint i = 0; i < SHADOW_SAMPLES; i++)
    {
        float2 sample_2d  = frac(xi_base + (float)i * float2(0.7548776662f, 0.5698402909f));
        float2 disk       = concentric_disk_sample(sample_2d);
        float3 sample_dir = sample_sun_direction(light_dir, disk, SUN_ANGULAR_RADIUS);
        
        float  accumulated_alpha = 0.0f;
        float  first_hit_dist    = -1.0f;
        
        if (!scene_has_transparents)
        {
            // sigma needs the nearest blocker for the penumbra width
            RayDesc ray;
            ray.Origin    = ray_origin;
            ray.Direction = sample_dir;
            ray.TMin      = 0.001f;
            ray.TMax      = SHADOW_RAY_MAX_DISTANCE;
            
            ShadowPayload payload;
            payload.hit_distance = -1.0f;
            payload.shadow_alpha = 0.0f;
            
            TraceRay(
                tlas,
                RAY_FLAG_FORCE_OPAQUE,
                0xFF,
                0,
                1,
                0,
                ray,
                payload
            );
            
            // read payload unconditionally so the compiler sees both fields accessed after trace
            float local_hit_distance = payload.hit_distance;
            float local_shadow_alpha = payload.shadow_alpha;
            
            if (local_hit_distance >= 0.0f)
            {
                // any hit in an opaque scene blocks fully, the max keeps the alpha read alive
                accumulated_alpha = max(local_shadow_alpha, 1.0f);
                first_hit_dist    = local_hit_distance;
            }
        }
        else
        {
            float3 current_origin = ray_origin;
            
            for (uint layer = 0; layer < MAX_TRANSPARENT_LAYERS; layer++)
            {
                RayDesc ray;
                ray.Origin    = current_origin;
                ray.Direction = sample_dir;
                ray.TMin      = 0.001f;
                ray.TMax      = SHADOW_RAY_MAX_DISTANCE;
                
                ShadowPayload payload;
                payload.hit_distance = -1.0f;
                payload.shadow_alpha = 0.0f;
                
                // no any hit shader exists so traversal can treat all geometry as opaque
                TraceRay(tlas, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 1, 0, ray, payload);
                
                // read payload unconditionally so the compiler sees both fields accessed after trace
                float local_hit_distance = payload.hit_distance;
                float local_shadow_alpha = payload.shadow_alpha;
                
                // no more blockers along this ray
                if (local_hit_distance < 0.0f)
                    break;
                
                // track the first hit for penumbra estimation
                if (first_hit_dist < 0.0f)
                    first_hit_dist = local_hit_distance;
                
                // opaque blocker, fully shadowed, no need to trace further
                if (local_shadow_alpha >= 1.0f)
                {
                    accumulated_alpha = 1.0f;
                    break;
                }
                
                // transparent surface, accumulate opacity and continue past it
                accumulated_alpha = 1.0f - (1.0f - accumulated_alpha) * (1.0f - local_shadow_alpha);
                if (accumulated_alpha >= 0.99f)
                {
                    accumulated_alpha = 1.0f;
                    break;
                }
                
                // advance past this surface
                current_origin = current_origin + sample_dir * (local_hit_distance + 0.01f);
            }
        }
        
        sum_visibility += 1.0f - accumulated_alpha;
        
        if (first_hit_dist > 0.0f && accumulated_alpha > 0.0f)
        {
            // accumulate the blocker distance weighted by opacity for spatial penumbra reconstruction
            sum_blocker    += first_hit_dist * accumulated_alpha;
            blocker_weight += accumulated_alpha;
        }
    }
    
    float visibility = sum_visibility / (float)SHADOW_SAMPLES;
    float hit_dist   = blocker_weight > 0.0f ? (sum_blocker / blocker_weight) : 0.0f;
    
    // r = raw visibility, g = blocker distance for the penumbra aware spatial filter
    tex_uav[launch_id] = float4(visibility, hit_dist, 0.0f, 1.0f);
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
