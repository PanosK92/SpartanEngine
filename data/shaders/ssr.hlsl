/*
Copyright(c) 2016-2024 Panos Karabelas

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

static const float g_ssr_depth_threshold = 8.0f;

uint compute_step_count(float roughness)
{
    float steps_min = 8.0;    // for very rough surfaces
    float steps_max = 128.0f; // for very reflective surfaces

    return (uint)lerp(steps_max, steps_min, roughness);
}

float compute_alpha(uint2 screen_pos, float2 hit_uv, float v_dot_r)
{
    float alpha = 1.0f;

    alpha *= screen_fade(hit_uv);                                // fade toward the edges of the screen
    alpha *= is_valid_uv(hit_uv);                                // fade if the uv is invalid
    alpha  = lerp(alpha, 0.0f, smoothstep(0.9f, 1.0f, v_dot_r)); // fade when facing the camera

    return saturate(alpha);
}

float get_depth_from_ray(float2 ray_pos, float2 ray_start, float ray_length, float z_start, float z_end)
{
    float alpha = length(ray_pos - ray_start) / ray_length;
    return (z_start * z_end) / lerp(z_end, z_start, alpha);
}

bool intersect_depth_buffer(float2 ray_pos, float2 ray_start, float ray_length, float z_start, float z_end, out float depth_delta)
{
    float depth_ray  = get_depth_from_ray(ray_pos, ray_start, ray_length, z_start, z_end);
    float depth_real = get_linear_depth(ray_pos);
    depth_delta      = (depth_ray - depth_real);

    return depth_delta >= 0.0f;
}

float3 comute_ray_end(float3 ray_start_vs, float3 ray_dir_vs)
{
    // calculate intersection with the near and far planes of the view frustum
    // t_near and t_far represent the distances along the ray to these intersections
    float t_near = (buffer_frame.camera_near - ray_start_vs.z) / ray_dir_vs.z;
    float t_far  = (buffer_frame.camera_far - ray_start_vs.z) / ray_dir_vs.z;

    // determine which of these distances to use based on their positivity
    // the goal is to find the point where the ray first intersects the frustum
    float t = (t_near > 0) ? t_near : t_far;
    if (t <= 0) // If both distances are negative, take the larger one
    {
        t = max(t_near, t_far);
    }

    // calculate the intersection point using the determined distance (t)
    // this is the point where the ray either enters or exits the camera's view frustum
    return ray_start_vs + ray_dir_vs * t;
}

float2 trace_ray(uint2 screen_pos, float3 ray_start_vs, float3 ray_dir_vs, float roughness, out float reflection_distance)
{
    float3 ray_end_vs       = comute_ray_end(ray_start_vs, ray_dir_vs);
    float2 ray_start        = view_to_uv(ray_start_vs);
    float2 ray_end          = view_to_uv(ray_end_vs);
    uint step_count         = compute_step_count(roughness);
    float2 ray_start_to_end = ray_end - ray_start;
    float ray_length        = length(ray_start_to_end);
    float2 ray_step_uv      = (ray_start_to_end + FLT_MIN) / (float)(step_count);
    float3 ray_step_vs      = (ray_end_vs - ray_start_vs) / (float)(step_count);
    float2 ray_pos          = ray_start;

    // adjust position with some noise
    float offset = get_noise_interleaved_gradient(screen_pos, false, false);
    ray_pos      += ray_step_uv * offset;
    
    // adaptive ray-marching variables
    const float min_step_size = 0.1f;          // minimum step size
    const float max_step_size = 1.0f;          // maximum step size
    float current_step_size   = max_step_size; // start with the largest step

    // binary search variables
    float depth_delta = 0.0f;
    float step_size   = 1.0;

    // ray-march
    reflection_distance = 0.0f;
    for (uint i = 0; i < step_count; i++)
    {
        // early exit if the ray is out of screen
        if (!is_valid_uv(ray_pos))
            return -1.0f;
        
        if (intersect_depth_buffer(ray_pos, ray_start, ray_length, ray_start_vs.z, ray_end_vs.z, depth_delta))
        {
            // adjust step size based on depth delta
            float depth_difference = abs(depth_delta);
            current_step_size      = lerp(min_step_size, max_step_size, depth_difference / g_ssr_depth_threshold);
            current_step_size      = max(current_step_size, min_step_size);

            // test if we are within the threshold
            if (depth_difference <= g_ssr_depth_threshold)
                return ray_pos;
            
            // adjust ray position
            ray_pos += sign(depth_delta) * ray_step_uv * current_step_size;
        }
        else
        {
            // reset step size to max if not intersecting
            current_step_size  = max_step_size;
            ray_pos           += ray_step_uv * current_step_size;
        }

         reflection_distance += length(ray_step_vs * current_step_size);
    }

    return -1.0f;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    // initialize to zero, it means no hit
    tex_uav[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
 
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);
    
    // compute early exit cases
    bool early_exit_1 = pass_is_opaque() && surface.is_transparent(); // if this is an opaque pass, ignore all transparent pixels
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque(); // if this is a transparent pass, ignore all opaque pixels
    bool early_exit_3 = surface.is_sky();                             // skip sky pixels
    if (early_exit_1 || early_exit_2 || early_exit_3)
        return;

    // compute reflection direction in view space
    float3 normal          = world_to_view(surface.normal, false);
    float3 position        = world_to_view(surface.position, true);
    float3 camera_to_pixel = normalize(position);
    float3 reflection      = normalize(reflect(camera_to_pixel, normal));
    float v_dot_r          = dot(-camera_to_pixel, reflection);

    // trace
    float reflection_distance  = 0.0f;
    float2 hit_uv              = trace_ray(thread_id.xy, position, reflection, surface.roughness, reflection_distance);
    hit_uv                    -= get_velocity_uv(hit_uv); // reproject
    float alpha                = compute_alpha(thread_id.xy, hit_uv, v_dot_r);
    float3 reflection_color    = tex.SampleLevel(samplers[sampler_bilinear_clamp], hit_uv, 0).rgb * alpha; // modulate with alpha because invalid UVs will get clamped colors

    // determine reflection roughness
    float max_reflection_distance = 1.0f;
    float distance_attenuation    = smoothstep(0.0f, max_reflection_distance, reflection_distance);
    float reflection_roughness    = lerp(surface.roughness, clamp(surface.roughness * 1.5f, 0.0f, 1.0f), distance_attenuation);
    reflection_roughness          = surface.roughness;

    tex_uav[thread_id.xy]  = float4(reflection_color, alpha);
    tex_uav2[thread_id.xy] = (reflection_roughness * reflection_roughness) * 10.0f;
}
