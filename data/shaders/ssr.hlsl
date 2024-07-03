//= INCLUDES =========
#include "common.hlsl"
//====================

// Constants
static const float g_ssr_depth_threshold_near = 0.1f;    // Depth threshold for near objects
static const float g_ssr_depth_threshold_far  = 20.0f;   // Depth threshold for far objects
static const float g_ssr_distance_near        = 10.0f;   // Distance considered as "near"
static const float g_ssr_distance_far         = 1000.0f; // Distance considered as "far"
static const float g_ssr_max_step_size        = 10.0f;   // Maximum step size for ray marching
static const float g_ssr_step_growth_factor   = 0.005f;  // Factor for gradually increasing step size
static const float g_ssr_fade_start           = 0.9f;
static const float g_ssr_fade_end             = 1.0f;
static const float g_ssr_steps_min            = 8.0f;    // for very rough surfaces
static const float g_ssr_steps_max            = 256.0f;  // for very reflective surfaces

uint compute_step_count(float roughness)
{
    return (uint)lerp(g_ssr_steps_max, g_ssr_steps_min, roughness);
}

float compute_alpha(uint2 screen_pos, float2 hit_uv, float v_dot_r)
{
    float alpha = 1.0f;

    alpha *= screen_fade(hit_uv);                                                      // fade toward the edges of the screen
    alpha *= is_valid_uv(hit_uv);                                                      // fade if the uv is invalid
    alpha  = lerp(alpha, 0.0f, smoothstep(g_ssr_fade_start, g_ssr_fade_end, v_dot_r)); // fade when facing the camera

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

float3 compute_ray_end(float3 ray_start_vs, float3 ray_dir_vs)
{
    float t_near = (buffer_frame.camera_near - ray_start_vs.z) / ray_dir_vs.z;
    float t_far  = (buffer_frame.camera_far - ray_start_vs.z) / ray_dir_vs.z;

    float t = (t_near > 0) ? t_near : t_far;
    t = (t <= 0) ? max(t_near, t_far) : t;

    return ray_start_vs + ray_dir_vs * t;
}

float get_distance_based_depth_threshold(float distance)
{
    return lerp(g_ssr_depth_threshold_near, g_ssr_depth_threshold_far, 
                saturate((distance - g_ssr_distance_near) / (g_ssr_distance_far - g_ssr_distance_near)));
}

float2 trace_ray(uint2 screen_pos, float3 ray_start_vs, float3 ray_dir_vs, float roughness, out float reflection_distance)
{
    float3 ray_end_vs       = compute_ray_end(ray_start_vs, ray_dir_vs);
    float2 ray_start        = view_to_uv(ray_start_vs);
    float2 ray_end          = view_to_uv(ray_end_vs);
    uint step_count         = compute_step_count(roughness);
    float2 ray_start_to_end = ray_end - ray_start;
    float ray_length        = length(ray_start_to_end);
    float2 ray_step_uv      = (ray_start_to_end + FLT_MIN) / (float)(step_count);
    float3 ray_step_vs      = (ray_end_vs - ray_start_vs) / (float)(step_count);
    float2 ray_pos          = ray_start;

    float offset  = get_noise_interleaved_gradient(screen_pos, true, true);
    ray_pos      += ray_step_uv * offset;
    
    float current_step_size = g_ssr_depth_threshold_near;  // Start with smallest step

    float depth_delta = 0.0f;

    reflection_distance = 0.0f;
    float total_distance = 0.0f;
    [loop]
    for (uint i = 0; i < step_count; ++i)
    {
        if (!is_valid_uv(ray_pos))
            return -1.0f;
        
        float current_distance = length(ray_start_vs + ray_dir_vs * total_distance);
        float current_depth_threshold = get_distance_based_depth_threshold(current_distance);
        
        if (intersect_depth_buffer(ray_pos, ray_start, ray_length, ray_start_vs.z, ray_end_vs.z, depth_delta))
        {
            float depth_difference = abs(depth_delta);
            if (depth_difference <= current_depth_threshold)
                return ray_pos;
            
            // Adaptive step size based on current distance and depth difference
            float distance_factor = saturate(total_distance / ray_length);
            float depth_factor = saturate(depth_difference / current_depth_threshold);
            current_step_size = lerp(g_ssr_depth_threshold_near, g_ssr_max_step_size, max(distance_factor, depth_factor));
            
            ray_pos += sign(depth_delta) * ray_step_uv * current_step_size;
        }
        else
        {
            // Gradually increase step size as we move away from the start
            current_step_size = lerp(current_step_size, g_ssr_max_step_size, g_ssr_step_growth_factor);
            ray_pos += ray_step_uv * current_step_size;
        }

        float step_distance = length(ray_step_vs * current_step_size);
        reflection_distance += step_distance;
        total_distance += step_distance;
    }

    return -1.0f;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    tex_uav[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);

    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);
    
    if ((pass_is_opaque() && surface.is_transparent()) || 
        (pass_is_transparent() && surface.is_opaque()) || 
        surface.is_sky())
    {
        return;
    }

    float3 normal          = world_to_view(surface.normal, false);
    float3 position        = world_to_view(surface.position, true);
    float3 camera_to_pixel = normalize(position);
    float3 reflection      = normalize(reflect(camera_to_pixel, normal));
    float v_dot_r          = dot(-camera_to_pixel, reflection);

    float reflection_distance  = 0.0f;
    float2 hit_uv              = trace_ray(thread_id.xy, position, reflection, surface.roughness, reflection_distance);
    hit_uv                    -= get_velocity_uv(hit_uv);
    float alpha                = compute_alpha(thread_id.xy, hit_uv, v_dot_r);
    float3 reflection_color    = tex.SampleLevel(samplers[sampler_bilinear_clamp], hit_uv, 0).rgb * alpha;

    tex_uav[thread_id.xy]  = float4(reflection_color, alpha);
}



