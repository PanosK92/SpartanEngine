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
    float x = 0.0f, y = 0.0f;
    float f_x = 0.5f, f_y = 1.0f / 3.0f;
    uint i_x = index, i_y = index;
    
    while (i_x > 0)
    {
        x   += f_x * (i_x % 2);
        i_x /= 2;
        f_x *= 0.5f;
    }
    
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
    float camera_distance = length(buffer_frame.camera_position - pos_ws);
    float base_offset     = 0.01f + camera_distance * 0.0001f;
    float3 ray_origin     = pos_ws + normal_ws * base_offset;
    
    // temporal jitter
    float temporal_offset = noise_interleaved_gradient(float2(launch_id), true);
    uint frame_offset     = buffer_frame.frame % 64;
    
    // contact-hardening soft shadows
    static const uint TOTAL_SAMPLES = 16;
    
    float avg_blocker_dist   = 0.0f;
    float blocker_count      = 0.0f;
    float hit_distances[TOTAL_SAMPLES];
    float shadow_alphas[TOTAL_SAMPLES];
    
    // blocker search - trace through transparent surfaces to find opaque blockers behind them
    static const uint MAX_TRANSPARENT_LAYERS = 4;
    
    for (uint i = 0; i < TOTAL_SAMPLES; i++)
    {
        float2 sample_2d  = halton_2d(i + frame_offset * TOTAL_SAMPLES);
        sample_2d         = frac(sample_2d + temporal_offset);
        float2 disk       = concentric_disk_sample(sample_2d);
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
            
            TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
            
            // no more blockers along this ray
            if (payload.hit_distance < 0.0f)
                break;
            
            // track the first hit for penumbra estimation
            if (first_hit_dist < 0.0f)
                first_hit_dist = payload.hit_distance;
            
            // opaque blocker - fully shadowed, no need to trace further
            if (payload.shadow_alpha >= 1.0f)
            {
                accumulated_alpha = 1.0f;
                break;
            }
            
            // transparent surface - accumulate opacity and continue past it
            accumulated_alpha = 1.0f - (1.0f - accumulated_alpha) * (1.0f - payload.shadow_alpha);
            if (accumulated_alpha >= 0.99f)
            {
                accumulated_alpha = 1.0f;
                break;
            }
            
            // advance past this surface
            current_origin = current_origin + sample_dir * (payload.hit_distance + 0.01f);
        }
        
        hit_distances[i] = first_hit_dist;
        shadow_alphas[i] = accumulated_alpha;
        
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
    
    // weighted visibility
    float weighted_visibility = 0.0f;
    float total_weight        = 0.0f;
    
    for (uint j = 0; j < TOTAL_SAMPLES; j++)
    {
        float2 sample_2d  = halton_2d(j + frame_offset * TOTAL_SAMPLES);
        sample_2d         = frac(sample_2d + temporal_offset);
        float2 disk       = concentric_disk_sample(sample_2d);
        float sample_dist = length(disk);
        
        float weight = lerp(1.0f, 1.0f - sample_dist * 0.5f, penumbra_size);
        weight       = max(weight, 0.1f);
        
        // use shadow alpha for partial transparency (0 = fully lit, 1 = fully shadowed)
        float sample_visibility = 1.0f - shadow_alphas[j];
        weighted_visibility    += sample_visibility * weight;
        total_weight           += weight;
    }
    
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
