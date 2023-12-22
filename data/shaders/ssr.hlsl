/*
Copyright(c) 2016-2023 Panos Karabelas

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

static const uint g_ssr_binary_search_steps = 64;
static const float g_ssr_thickness          = 15.0f;

uint compute_step_count(float roughness)
{
    float steps_min = 8.0;    // for very rough surfaces
    float steps_max = 128.0f; // for very reflective surfaces

    return (uint)lerp(steps_max, steps_min, roughness);
}

float compute_alpha(uint2 screen_pos, float2 hit_uv, float v_dot_r)
{
    float alpha = 1.0f;

    alpha *= screen_fade(hit_uv); // fade toward the edges of the screen
    alpha *= all(hit_uv);         // fade if the uv is invalid

    return alpha;
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

float2 trace_ray(uint2 screen_pos, float3 ray_start_vs, float3 ray_dir_vs, float roughness)
{
    // compute ray end and start depth
    float3 ray_end_vs = ray_start_vs + ray_dir_vs * buffer_frame.camera_far;
    float depth_end   = ray_end_vs.z;
    float depth_start = ray_start_vs.z;
    
    // compute ray start and end (in UV space)
    float2 ray_start = view_to_uv(ray_start_vs);
    float2 ray_end   = view_to_uv(ray_end_vs);

    // compute ray step
    uint step_count         = compute_step_count(roughness);
    float2 ray_start_to_end = ray_end - ray_start;
    float ray_length        = length(ray_start_to_end);
    float2 ray_step         = (ray_start_to_end + FLT_MIN) / (float)(step_count);
    float2 ray_pos          = ray_start;

    // adjust position with some noise
    float offset = get_noise_interleaved_gradient(screen_pos, true, false);
    ray_pos      += ray_step * offset;
    
    // improved binary search variables
    float depth_delta = 0.0f;
    float step_size   = 1.0; // initial step size

    // ray-march
    for (uint i = 0; i < step_count; i++)
    {
        // early exit if the ray is out of screen
        if (!is_valid_uv(ray_pos))
            return 0.0f;
        
        // intersect depth buffer
        if (intersect_depth_buffer(ray_pos, ray_start, ray_length, depth_start, depth_end, depth_delta))
        {
            // smaller steps as we get closer to the actual intersection
            step_size *= 0.5f;

            // test if we are within the threshold
            if (abs(depth_delta) <= g_ssr_thickness)
                return ray_pos;

            // adjust ray position
            ray_pos += sign(depth_delta) * ray_step * step_size;
        }
        else
        {
            // increase step size if not intersecting
            step_size = min(step_size * 2.0f, 1.0f);
            ray_pos += ray_step * step_size;
        }
    }

    return 0.0f;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    // initialize to zero, it means no hit
    tex_uav[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
 
    Surface surface;
    surface.Build(thread_id.xy, true, false, false);

    // compute reflection direction in view space
    float3 normal          = world_to_view(surface.normal, false);
    float3 position        = world_to_view(surface.position, true);
    float3 camera_to_pixel = normalize(position);
    float3 reflection      = normalize(reflect(camera_to_pixel, normal));
    float v_dot_r          = dot(-camera_to_pixel, reflection);

    // compute early exit cases
    bool early_exit_1 = pass_is_opaque() && surface.is_transparent(); // if this is an opaque pass, ignore all transparent pixels
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque(); // if this is a transparent pass, ignore all opaque pixels
    bool early_exit_3 = surface.is_sky();                             // skip sky pixels
    bool early_exit_4 = v_dot_r >= 0.0f;                              // skip camera facing rays
    if (early_exit_1 || early_exit_2 || early_exit_3 || early_exit_4)
        return;

    // trace
    float2 hit_uv = trace_ray(thread_id.xy, position, reflection, surface.roughness);
    float alpha   = compute_alpha(thread_id.xy, hit_uv, v_dot_r);

    // sample scene color
    hit_uv          -= get_velocity_ndc(hit_uv); // reproject
    bool valid_uv    = hit_uv.x != -1.0f && hit_uv.y != -1.0f;
    bool valid_alpha = alpha != 0.0f;
    float3 color     = (valid_uv && valid_alpha) ? tex.SampleLevel(samplers[sampler_bilinear_clamp], hit_uv, 0).rgb : 0.0f;

    tex_uav[thread_id.xy] = float4(color, alpha);
}
