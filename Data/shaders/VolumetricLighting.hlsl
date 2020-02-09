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

static const uint g_vl_steps 		= 64;
static const float g_vl_scattering 	= 0.998f;
static const float g_vl_pow			= 0.4f;

// Mie scaterring approximated with Henyey-Greenstein phase function.
float vl_compute_scattering(float v_dot_l)
{
    static const float vl_scattering2   = g_vl_scattering * g_vl_scattering;
	static const float result           = 1.0f - vl_scattering2;
	float e                             = abs(1.0f + vl_scattering2 - (2.0f * g_vl_scattering) * v_dot_l);
	return result / pow(e, g_vl_pow);
}

float3 vl_raymarch(Light light, float3 ray_pos, float3 ray_step, float ray_dot_light, int array_index)
{
	float3 fog = 0.0f;
    
	for (uint i = 0; i < g_vl_steps; i++)
	{
		// Compute position in clip space
        float3 pos = project(ray_pos, light_view_projection[light.index][array_index]);
        
		// Check to see if the light can "see" the pixel
        #ifdef DIRECTIONAL
		float depth_delta = compare_depth(float3(pos.xy, array_index), pos.z + light.bias);
        #elif POINT
        float depth_delta = compare_depth(normalize(ray_pos - light.position), pos.z + light.bias);
        #elif SPOT
        float depth_delta = compare_depth(float3(pos.xy, array_index), pos.z + light.bias);
        #endif
       
		if (depth_delta > 0.0f)
		{
			fog += vl_compute_scattering(ray_dot_light);
		}
		
		ray_pos += ray_step;
	}

	return fog / (float)g_vl_steps;
}

float3 VolumetricLighting(Light light, float3 pos_world, float2 uv)
{
	float3 pixel_to_camera 			= g_camera_position.xyz - pos_world;
	float pixel_to_camera_length 	= length(pixel_to_camera);
	float3 ray_dir					= pixel_to_camera / pixel_to_camera_length;
	float step_length 				= pixel_to_camera_length / (float)g_vl_steps;
	float3 ray_step 				= ray_dir * step_length;
	float3 ray_pos 					= pos_world;
	float ray_dot_light				= dot(ray_dir, light.direction);
	float3 fog 						= 0.0f;
    
	// Apply dithering as it will allows us to get away with a crazy low sample count ;-)
	float3 dither_value = dither(uv) * 100;
	ray_pos += ray_step * dither_value;
	
	for (uint array_index = 0; array_index < light.array_size; array_index++)
	{
        // Compute position in clip space
        float3 pos = project(ray_pos, light_view_projection[light.index][array_index]);
        
		[branch]
		if (is_saturated(pos))
		{
			// Ray-march
			fog += vl_raymarch(light, ray_pos, ray_step, ray_dot_light, array_index);
            
            // If we are close to the edge of the primary cascade and a next cascade exists, lerp with it.
            float cascade_lerp = (max3(abs(pos)) - 0.9f);
            [branch]
            if (light.is_directional && cascade_lerp > 0.0f && array_index < light.array_size - 1)
            {
                // Ray-march using the next cascade
                float3 fog_secondary = vl_raymarch(light, ray_pos, ray_step, ray_dot_light, array_index + 1);
                
                // Blend cascades	
                fog = lerp(fog, fog_secondary, cascade_lerp);
                break;
            }
		}
	}
	
	return fog * light.color * light.intensity;
}