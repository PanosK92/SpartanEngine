/*
Copyright(c) 2016-2021 Panos Karabelas

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
    REGULAR FOG
------------------------------------------------------------------------------*/

#if FOG_REGULAR

static const float g_fog_start        = -0.5f;
static const float g_fog_end          = 5.0f;
static const float g_fog_start_height = -0.5f;
static const float g_fog_end_height   = 7.0f;

float get_fog_factor(const float pixel_y, const float pixel_z)
{
    //float depth_factor  = saturate(1.0f - (fog_end - pixel_z)           / (fog_end - fog_start + FLT_MIN));
    //float height_factor = saturate(1.0f - (fog_end_height - pixel_y)    / (fog_end_height - fog_start_height + FLT_MIN));

    // I'm better off introducing fog volumes so te user can define depth
    // and height factors instead of using some hardcoded values here (which don't work all that well).
    return buffer_frame.fog_density;
}

float get_fog_factor(const Surface surface)
{
    return get_fog_factor(surface.position.y, surface.camera_to_pixel_length);
}

#endif
/*------------------------------------------------------------------------------
    VOLUMETRIC FOG
------------------------------------------------------------------------------*/

#if FOG_VOLUMETRIC

static const uint g_vl_steps                    = 16;
static const float g_vl_scattering              = 0.8f; // [0, 1]
static const float g_vl_pow                     = 1000.0f;
static const float g_vl_cascade_blend_threshold = 0.1f;
static const float g_vl_steps_rcp               = 1.0f / g_vl_steps;
static const float g_vl_scattering2             = g_vl_scattering * g_vl_scattering;
static const float g_vl_x                       = 1 - g_vl_scattering2;
static const float g_vl_y                       = 1 + g_vl_scattering2;

float compute_mie_scattering(float v_dot_l)
{
    float e = abs(g_vl_y - g_vl_scattering2 * v_dot_l);
    return g_vl_x / pow(e, g_vl_pow);
}

float3 vl_raymarch(Light light, float3 ray_pos, float3 ray_step, float3 ray_dir, int cascade_index)
{
    float3 fog_accumulation= 0.0f;

    for (uint i = 0; i < g_vl_steps; i++)
    {
        float3 fog = 1.0f;

        // Attenuate
        if (light_is_directional())
        {
            fog *= compute_mie_scattering(dot(light.to_pixel, ray_dir));
        }
        else
        {
            fog *= light.attenuation;
        }

        float3 pos_ndc = 0.0f;
        if (light_has_shadows() || light_has_shadows_transparent())
        {
            pos_ndc = world_to_ndc(ray_pos, cb_light_view_projection[cascade_index]);
        }

        // Shadows - Opaque
        if (light_has_shadows())
        {
            if (light_is_point())
            {
                fog *= shadow_compare_depth(normalize(ray_pos - light.position), pos_ndc.z);
            }
            else // directional & spot
            {
                fog *= shadow_compare_depth(float3(ndc_to_uv(pos_ndc), cascade_index), pos_ndc.z);
            }
        }

        // Shadows - Transparent
        if (light_has_shadows_transparent())
        {
            if (light_is_point())
            {
                fog *= shadow_sample_color(normalize(ray_pos - light.position));
            }
            else // directional & spot
            {
                fog *= shadow_sample_color(float3(ndc_to_uv(pos_ndc), cascade_index));
            }
        }

        // Accumulate
        fog_accumulation += fog;

        // Step
        ray_pos += ray_step;
    }

    return saturate(fog_accumulation * g_vl_steps_rcp);
}

float3 VolumetricLighting(Surface surface, Light light)
{
    float3 fog        = 0.0f;
    float3 ray_pos    = surface.position;         // pixel
    float3 ray_dir    = -surface.camera_to_pixel; // to camera
    float step_length = surface.camera_to_pixel_length / (float)g_vl_steps;
    float3 ray_step   = ray_dir * step_length;
    
    // Offset ray to get away with way less steps and great detail
    float offset = get_noise_interleaved_gradient(surface.uv * buffer_frame.shadow_resolution);
    ray_pos += ray_step * offset;

    if (light_is_directional())
    {
        for (uint cascade_index = 0; cascade_index < light.array_size; cascade_index++)
        {
            // Project into light space
            float3 pos_ndc = world_to_ndc(ray_pos, cb_light_view_projection[cascade_index]);
            float2 pos_uv  = ndc_to_uv(pos_ndc);
        
            // Ensure not out of bound
            if (is_saturated(pos_uv))
            {
                // Ray-march
                fog += vl_raymarch(light, ray_pos, ray_step, ray_dir, cascade_index);
        
                // If we are close to the edge a secondary cascade exists, lerp with it.
                float cascade_fade = (max2(abs(pos_ndc.xy)) - g_shadow_cascade_blend_threshold) * 4.0f;
                cascade_index++;
                if (cascade_fade > 0.0f && cascade_index < light.array_size - 1)
                {
                    // Ray-march using the next cascade
                    float3 fog_secondary = vl_raymarch(light, ray_pos, ray_step, ray_dir, cascade_index);
        
                    // Blend cascades
                    fog = lerp(fog, fog_secondary, cascade_fade);
                    break;
                }
                break;
            }
        }
    }
    else // POINT/SPOT
    {
        uint projection_index = 0;

        if (light_is_point())
        {
            projection_index = direction_to_cube_face_index(light.to_pixel);
        }
    
        fog = vl_raymarch(light, ray_pos, ray_step, ray_dir, projection_index);
    }

    float fog_regular     = get_fog_factor(surface);
    float3 fog_volumetric = fog * light.color * light.intensity * light.attenuation;

    return fog_regular * fog_volumetric;
}
#endif
