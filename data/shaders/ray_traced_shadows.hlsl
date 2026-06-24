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

static const float SUN_ANGULAR_RADIUS = 0.00465f * 2.0f; // sun disk size in radians

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

float2 halton_2d(uint index)
{
    // base 2 radical inverse is just a bit reversal scaled by 1 / 2^32
    float x = reversebits(index) * 2.3283064365386963e-10f;
    
    float y = 0.0f, f_y = 1.0f / 3.0f;
    uint i_y = index;
    while (i_y > 0)
    {
        y   += f_y * (i_y % 3);
        i_y /= 3;
        f_y /= 3.0f;
    }
    
    return float2(x, y);
}

[shader("raygeneration")]
void ray_gen()
{
    uint2 launch_id   = DispatchRaysIndex().xy;
    uint2 launch_size = DispatchRaysDimensions().xy;
    float2 uv         = (launch_id + 0.5f) / launch_size;
    
    // early out for sky pixels
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
        tex_uav[launch_id] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    float3 light_dir = normalize(-light_parameters[0].direction);
    
    // backface culling
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
    
    // contact hardening soft shadows
    static const uint TOTAL_SAMPLES          = 64;
    static const uint MAX_TRANSPARENT_LAYERS = 4;
    
    float avg_blocker_dist = 0.0f;
    float blocker_count    = 0.0f;
    
    // penumbra weighting is linear in the disk radius so its sums are accumulated inline
    // instead of caching per sample results, this avoids two large arrays and a second pass
    float sum_visibility      = 0.0f;
    float sum_dist            = 0.0f;
    float sum_visibility_dist = 0.0f;
    
    for (uint i = 0; i < TOTAL_SAMPLES; i++)
    {
        float2 sample_2d  = halton_2d(i + 1);
        float2 disk       = concentric_disk_sample(sample_2d);
        float  disk_dist  = length(disk);
        float3 sample_dir = sample_sun_direction(light_dir, disk, SUN_ANGULAR_RADIUS);
        
        float  accumulated_alpha = 0.0f;
        float  first_hit_dist    = -1.0f;
        float3 current_origin    = ray_origin;
        
        for (uint layer = 0; layer < MAX_TRANSPARENT_LAYERS; layer++)
        {
            RayDesc ray;
            ray.Origin    = current_origin;
            ray.Direction = sample_dir;
            ray.TMin      = 0.001f;
            ray.TMax      = 10000.0f;
            
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
        
        float visibility     = 1.0f - accumulated_alpha;
        sum_visibility      += visibility;
        sum_dist            += disk_dist;
        sum_visibility_dist += visibility * disk_dist;
        
        if (first_hit_dist > 0.0f && accumulated_alpha > 0.0f)
        {
            avg_blocker_dist += first_hit_dist;
            blocker_count    += accumulated_alpha; // weight by opacity
        }
    }
    
    // fully lit (no blockers at all)
    if (blocker_count < 0.01f)
    {
        tex_uav[launch_id] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    
    // fully shadowed (all samples hit fully opaque blockers)
    if (blocker_count >= float(TOTAL_SAMPLES) - 0.01f)
    {
        tex_uav[launch_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    
    avg_blocker_dist /= blocker_count;
    
    // penumbra width from blocker distance
    float penumbra_size = saturate(avg_blocker_dist / 100.0f);
    
    // weight per sample is 1 - 0.5 * penumbra_size * disk_dist, the original 0.1 clamp is
    // unreachable since disk_dist and penumbra_size are both in [0,1] so weight stays in [0.5,1]
    float k                   = 0.5f * penumbra_size;
    float total_weight        = float(TOTAL_SAMPLES) - k * sum_dist;
    float weighted_visibility = sum_visibility - k * sum_visibility_dist;
    
    float final_shadow = weighted_visibility / total_weight;
    tex_uav[launch_id] = float4(final_shadow, final_shadow, final_shadow, 1.0f);
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
