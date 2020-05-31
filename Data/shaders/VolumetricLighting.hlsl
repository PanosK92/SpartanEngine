/*
Copyright(c) 2016-2020 Panos Karabelas

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

static const uint g_vl_steps        = 4;
static const float g_vl_tolerance   = 0.45f;
static const float g_vl_scattering  = 0.994f;
static const float g_vl_pow         = 0.5f;

// Mie scaterring approximated with Henyey-Greenstein phase function.
float vl_compute_scattering(float v_dot_l)
{
    static const float vl_scattering2   = g_vl_scattering * g_vl_scattering;
    static const float result           = 1.0f - vl_scattering2;
    float e                             = abs(1.0f + vl_scattering2 - (2.0f * g_vl_scattering) * v_dot_l);
    return result / pow(e, g_vl_pow);
}

float vl_raymarch(Light light, float3 ray_pos, float3 ray_step, float ray_dot_light, int array_index)
{
    float fog = 0.0f;
    
    for (uint i = 0; i < g_vl_steps; i++)
    {
        // Compute position in clip space
        float3 pos = project(ray_pos, light_view_projection[array_index]);
        
        // Compare depth
        #if POINT
        float depth_delta = compare_depth(normalize(ray_pos - light.position), pos.z);
        #else // directional & spot
        float depth_delta = compare_depth(float3(pos.xy, array_index), pos.z);
        #endif
       
        // Depth test
        if (abs(g_vl_tolerance - depth_delta) > g_vl_tolerance)
        {
            fog += vl_compute_scattering(ray_dot_light);
        }
        
        ray_pos += ray_step;
    }

    return fog / (float)g_vl_steps;
}

float3 VolumetricLighting(Surface surface, Light light)
{
    float3 ray_pos      = surface.position;
    float3 ray_dir      = -surface.camera_to_pixel;
    float step_length   = surface.camera_to_pixel_length / (float)g_vl_steps;
    float3 ray_step     = ray_dir * step_length;   
    float ray_dot_light = dot(ray_dir, light.direction);
    float fog           = 0.0f;
    
    // Offset ray to get away with way less steps and great detail
    float offset = interleaved_gradient_noise(surface.uv * g_resolution);
    ray_pos += ray_step * offset;
    
    #if DIRECTIONAL
    {
		[loop]
        for (uint array_index = 0; array_index < light.array_size; array_index++)
        {
			// Compute position in clip space
			float3 pos = project(ray_pos, light_view_projection[array_index]);
		
			[branch]
			if (is_saturated(pos))
			{
				// Ray-march
				fog += vl_raymarch(light, ray_pos, ray_step, ray_dot_light, array_index);
				
				// If we are close to the edge of the primary cascade and a next cascade exists, lerp with it.
				static const float blend_threshold = 0.1f;
				float distance_to_edge = 1.0f - max3(abs(pos * 2.0f - 1.0f));
				uint array_index_secondary = array_index + 1;
				[branch]
				if (distance_to_edge < blend_threshold && array_index_secondary < light.array_size)
				{
					// Ray-march using the next cascade
					float fog_secondary = vl_raymarch(light, ray_pos, ray_step, ray_dot_light, array_index_secondary);
					
					// Blend cascades
					float alpha = smoothstep(0.0f, blend_threshold, distance_to_edge);
					fog = lerp(fog_secondary, fog, alpha);
				}
				break;
			}
			else
			{
				fog += vl_compute_scattering(ray_dot_light) * 0.25f;
			}
		}
    }
    #elif POINT
    {
        [branch]
        if (light.distance_to_pixel < light.range)
        {
            uint projection_index = direction_to_cube_face_index(light.direction);
            
            // Compute position in clip space
            float3 pos = project(ray_pos, light_view_projection[projection_index]);
            
            // Ray-march
            fog = vl_raymarch(light, ray_pos, ray_step, ray_dot_light, projection_index);
        }
    }
    #elif SPOT
    {
        [branch]
        if (light.distance_to_pixel < light.range)
        {
            // Compute position in clip space
            float3 pos = project(ray_pos, light_view_projection[0]);
            
            // Ray-march
            [branch]
            if (is_saturated(pos))
            {
                fog = vl_raymarch(light, ray_pos, ray_step, ray_dot_light, 0);
            }
        }
    }
    #endif
    
    return fog * light.color * light.intensity;
}


