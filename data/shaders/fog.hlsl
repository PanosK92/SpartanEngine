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

/*------------------------------------------------------------------------------
    FOG - RADIAL
------------------------------------------------------------------------------*/
float3 got_fog_radial(const float3 pixel_position, const float3 camera_position)
{
    // parameters
    const float g_fog_radius         = 150.0f; // how far away from the camera the fog starts
    const float g_fog_fade_rate      = 0.05f;  // higher values make the fog fade in more abruptly
    const float3 g_atmospheric_color = float3(0.4f, 0.4f, 0.8f);  // soft blue
    
    float distance_from_camera = length(pixel_position - camera_position) - g_fog_radius;
    float distance_factor      = max(0.0f, distance_from_camera) / g_fog_radius; // normalize the distance
    float fog_factor           = 1.0f - exp(-g_fog_fade_rate * distance_factor); // exponential fog factor
    float fog_density          = pass_get_f3_value().y;
    
    return fog_factor * fog_density * g_atmospheric_color;
}

/*------------------------------------------------------------------------------
    FOG - VOLUMETRIC
------------------------------------------------------------------------------*/
float sample_shadow_map(float3 uv)
{
    // float3 -> uv, slice
    if (light_is_directional())
        return tex_light_directional_depth.SampleLevel(samplers[sampler_point_clamp_edge], uv, 0).r;
    
    // float3 -> direction
    if (light_is_point())
        return tex_light_point_depth.SampleLevel(samplers[sampler_point_clamp_edge], uv, 0).r;

    // float3 -> uv, 0
    if (light_is_spot())
        return tex_light_spot_depth.SampleLevel(samplers[sampler_point_clamp_edge], uv.xy, 0).r;

    return 0.0f;
}

float visibility(float3 position, Light light, uint2 pixel_pos)
{
    // project to light space
    uint slice_index = light_is_point() ? direction_to_cube_face_index(light.to_pixel) : 0;
    float3 pos_ndc   = world_to_ndc(position, GetLight().view_projection[slice_index]);
    float2 pos_uv    = ndc_to_uv(pos_ndc);

    // shadow map comparison
    bool shadow_map_comparison = true;
    if (is_valid_uv(pos_uv))
    {
        float3 sample_coords  = light_is_point() ? light.to_pixel : float3(pos_uv.x, pos_uv.y, slice_index);
        float shadow_depth    = sample_shadow_map(sample_coords);
        shadow_map_comparison = pos_ndc.z <= shadow_depth;
    }

    return shadow_map_comparison ? 0.0f : 1.0f;
}

float3 compute_volumetric_fog(Surface surface, Light light, uint2 pixel_pos)
{
    // parameters
    const float fog_density = pass_get_f3_value().x * 0.00002f;
    const uint num_steps    = 128;
    
    float total_distance = surface.camera_to_pixel_length;
    float step_length    = total_distance / num_steps; 
    float3 ray_origin    = buffer_frame.camera_position;
    float3 ray_direction = normalize(surface.camera_to_pixel);
    float3 ray_step      = ray_direction * step_length;
    float3 ray_pos       = ray_origin + get_noise_interleaved_gradient(surface.uv * pass_get_resolution_out(), true, false) * 0.03f;

    float fog = 0.0f;
    [unroll]
    for (uint i = 0; i < num_steps; i++)
    {
        if (length(ray_pos - ray_origin) > total_distance)
            break;

        // accumulate fog
        fog += fog_density * visibility(ray_pos, light, pixel_pos);

        // step ray
        ray_pos += ray_step;
    }

    return fog * light.color * light.intensity * light.attenuation;
}
