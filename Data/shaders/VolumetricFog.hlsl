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

static const uint g_vl_steps                    = 8;
static const float g_vl_scattering              = 0.5f;
static const float g_vl_pow                     = 1.5f;
static const float g_vl_cascade_blend_threshold = 0.1f;

static const float g_vl_scattering2 = g_vl_scattering * g_vl_scattering;
static const float g_vl_x           = 1 - g_vl_scattering2;
static const float g_vl_y           = 1 + g_vl_scattering2;

float mie_scattering(float v_dot_l)
{
    float e = abs(g_vl_y - g_vl_scattering2 * v_dot_l);
    return g_vl_x / pow(e, g_vl_pow);
}

float3 vl_raymarch(Light light, float3 ray_pos, float3 ray_step, float3 ray_dir, int cascade_index)
{
    float3 fog = 0.0f;

    [unroll]
    for (uint i = 0; i < g_vl_steps; i++)
    {
        float3 attenuation = 1.0f;
        
        // Attenuate
        #if DIRECTIONAL
        attenuation *= mie_scattering(dot(-light.direction, ray_dir));
        #else
        attenuation *= light.compute_attenuation(ray_pos);
        float3 to_light = normalize(light.position - ray_pos);
        attenuation *= mie_scattering(dot(light.direction, -to_light));
        #endif

        #if SHADOWS == 1 ||  SHADOWS_TRANSPARENT == 1
        float3 pos_ndc = world_to_ndc(ray_pos, cb_light_view_projection[cascade_index]);
        #endif
        
        // Shadows - Opaque
        #if SHADOWS
        {
            #if POINT
            attenuation *= shadow_compare_depth(normalize(ray_pos - light.position), pos_ndc.z);
            #else // directional & spot
            attenuation *= shadow_compare_depth(float3(ndc_to_uv(pos_ndc), cascade_index), pos_ndc.z);
            #endif
        }
        #endif

        // Shadows - Transparent
        #if SHADOWS_TRANSPARENT
        {
            #if POINT
            attenuation *= shadow_sample_color(normalize(ray_pos - light.position)).rgb;
            #else // directional & spot
            attenuation *= shadow_sample_color(float3(ndc_to_uv(pos_ndc), cascade_index)).rgb;
            #endif
        }
        #endif

        // Integrate
        fog += attenuation;

        // Step
        ray_pos += ray_step;
    }

    return fog / (float)g_vl_steps;
}

float3 VolumetricLighting(Surface surface, Light light)
{
    float3 ray_pos      = g_camera_position.xyz;
    float3 ray_dir      = surface.camera_to_pixel;
    float step_length   = surface.camera_to_pixel_length / (float)g_vl_steps;
    float3 ray_step     = ray_dir * step_length;
    float3 fog          = 0.0f;

    // Offset ray to get away with way less steps and great detail
    float offset = get_noise_interleaved_gradient(surface.uv * g_shadow_resolution) * 2.0f - 1.0f;
    ray_pos += ray_step * offset;
    
    #if DIRECTIONAL
    { 
        [unroll]
        for (uint cascade_index = 0; cascade_index < light.array_size; cascade_index++)
        {
            // Project into light space
            float3 pos_ndc  = world_to_ndc(ray_pos, cb_light_view_projection[cascade_index]);
            float2 pos_uv   = ndc_to_uv(pos_ndc);

            // Ensure not out of bound
            [branch]
            if (is_saturated(pos_uv))
            {
                // Ray-march
                fog += vl_raymarch(light, ray_pos, ray_step, ray_dir, cascade_index);

                // If we are close to the edge a secondary cascade exists, lerp with it.
                float cascade_fade = (max2(abs(pos_ndc.xy)) - g_shadow_cascade_blend_threshold) * 4.0f;
                cascade_index++;
                [branch]
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
    #else
    {
        uint projection_index = 0;
        #if POINT
        projection_index = direction_to_cube_face_index(light.direction);
        #endif
        fog = vl_raymarch(light, ray_pos, ray_step, ray_dir, projection_index);
    }
    #endif

    return fog;
}
