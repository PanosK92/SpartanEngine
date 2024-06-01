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

// ideally I fetch this from my atmospheric texture
static const float3 g_atmospheric_color = float3(0.4f, 0.4f, 0.8f);

/*------------------------------------------------------------------------------
    FOG - RADIAL
------------------------------------------------------------------------------*/
float3 got_fog_radial(const float camera_to_pixel_length, const float3 camera_position, const float directional_light_intensity)
{
    // parameters
    const float g_fog_radius    = 150.0f; // how far away from the camera the fog starts
    const float g_fog_fade_rate = 0.05f;  // higher values make the fog fade in more abruptly
    
    float distance_from_camera = camera_to_pixel_length - g_fog_radius;
    float distance_factor      = max(0.0f, distance_from_camera) / g_fog_radius; // normalize the distance
    float fog_factor           = 1.0f - exp(-g_fog_fade_rate * distance_factor); // exponential fog factor
    float fog_density          = pass_get_f3_value().y;
    
    return fog_factor * fog_density * g_atmospheric_color * directional_light_intensity;
}

/*------------------------------------------------------------------------------
    FOG - VOLUMETRIC
------------------------------------------------------------------------------*/
float visibility(float3 position, Light light, uint2 pixel_pos)
{
    bool is_visible      = is_visible = light.is_directional(); // directioanl light is everywhere, so assume visible
    float2 projected_uv  = 0.0f;
    float3 projected_pos = 0.0f;

    // slice index
    float dot_result = dot(light.forward, light.to_pixel);
    uint slice_index = light.is_point() * step(0.0f, -dot_result);

    // projection and shadow map comparison
    if (light.is_point())
    {
        // project
        projected_pos = mul(float4(position, 1.0f), light.transform[slice_index]).xyz;
        float3 ndc    = project_onto_paraboloid(projected_pos, light.near, light.far);
        projected_uv  = ndc_to_uv(ndc.xy);

        // compare
        float3 sample_coords = float3(projected_uv, slice_index);
        float shadow_depth   = light.sample_depth(sample_coords);
        is_visible           = ndc.z > shadow_depth;
    }
    else
    {
        // project
        projected_pos = world_to_ndc(position, light.transform[slice_index]);
        projected_uv  = ndc_to_uv(projected_pos);

        // compare
        if (is_valid_uv(projected_uv))
        {
            float3 sample_coords = float3(projected_uv.x, projected_uv.y, slice_index);
            float shadow_depth   = light.sample_depth(sample_coords);
            is_visible           = projected_pos.z > shadow_depth;
        }
    }

    return is_visible ? 1.0f : 0.0f;
}

float3 compute_volumetric_fog(Surface surface, Light light, uint2 pixel_pos)
{
    // parameters
    const float fog_density = pass_get_f3_value().x * 0.03f;
    const uint step_count   = 64;

    const float total_distance = surface.camera_to_pixel_length;
    const float step_length    = total_distance / step_count; 
    const float3 ray_origin    = buffer_frame.camera_position;
    const float3 ray_direction = normalize(surface.camera_to_pixel);
    const float3 ray_step      = ray_direction * step_length;
    float3 ray_pos             = ray_origin + get_noise_interleaved_gradient(pixel_pos, true, true) * 0.1f;

    float fog = 0.0f;
    if (surface.is_sky())
    {
        fog = fog_density;
    }
    else
    {
        for (uint i = 0; i < step_count; i++)
        {
            fog     += fog_density * visibility(ray_pos, light, pixel_pos);
            ray_pos += ray_step;
        }
        fog /= float(step_count);
    }

    return fog * light.intensity * light.color * g_atmospheric_color;
}
