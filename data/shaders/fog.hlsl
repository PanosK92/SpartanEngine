/*
Copyright(c) 2015-2025 Panos Karabelas

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
    FOG - ATMOSPHERIC
------------------------------------------------------------------------------*/
float get_fog_atmospheric(const float camera_to_pixel_length, const float pixel_height_world)
{
    float camera_height = buffer_frame.camera_position.y;
    float density      = pass_get_f3_value().y * 0.0001f;
    float scale_height = 50.0f; 
    float b            = 1.0f / scale_height;
    float delta_height = pixel_height_world - camera_height;
    float dist         = camera_to_pixel_length;
    float rd_y         = (abs(dist) > 1e-6f) ? delta_height / dist : 0.0f;

    float tau = 0.0f;
    if (abs(rd_y) < 1e-5f)
    {
        // horizontal ray approximation
        tau = density * exp(-camera_height * b) * dist;
    }
    else
    {
        // analytical integral for optical depth in exponential atmosphere
        tau = density * exp(-camera_height * b) * (1.0f - exp(-dist * rd_y * b)) / (b * rd_y);
    }

    // beer's law for transmittance; fog factor is the in-scatter amount (1 - transmittance)
    return 1.0f - exp(-tau);
}

/*------------------------------------------------------------------------------
    FOG - VOLUMETRIC
------------------------------------------------------------------------------*/
float visible(float3 position, Light light, uint2 pixel_pos)
{
    bool is_visible      = is_visible = light.is_directional(); // directional light is everywhere, so assume visible
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
    const float  fog_density     = pass_get_f3_value().y * 0.03f;
    const uint   step_count      = 64;
    const float  total_distance  = surface.camera_to_pixel_length;
    const float  step_length     = total_distance / step_count;
    const float3 ray_origin      = buffer_frame.camera_position;
    const float3 ray_direction   = normalize(surface.camera_to_pixel);
    const float3 ray_step        = ray_direction * step_length;
          float3 ray_pos         = ray_origin + noise_interleaved_gradient(pixel_pos) * 0.1f;
          float  fog             = 0.0f;
          float  transmittance   = 1.0f;
    
    if (surface.is_sky())
    {
        fog = fog_density;
    }
    else
    {
        for (uint i = 0; i < step_count; i++)
        {
            fog     += fog_density * visible(ray_pos, light, pixel_pos) * light.compute_attenuation_volumetric(ray_pos);
            ray_pos  = ray_pos + ray_step;
        }
        fog /= float(step_count);
    }
    return fog * light.intensity * light.color;
}
