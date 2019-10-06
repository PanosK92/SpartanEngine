/*
Copyright(c) 2016-2019 Panos Karabelas

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

static const float g_vl_steps 		= 16;
static const float g_vl_scattering 	= 0.997f;
static const float g_vl_pow			= 0.5f;

// Mie scaterring approximated with Henyey-Greenstein phase function.
float ComputeScattering(float v_dot_l)
{
	float result = 1.0f - g_vl_scattering * g_vl_scattering;
	float e = abs(1.0f + g_vl_scattering * g_vl_scattering - (2.0f * g_vl_scattering) * v_dot_l);
	result /= pow(e, g_vl_pow);
	return result;
}

float3 vl_raymarch(float3 ray_pos, float3 ray_step, float ray_dot_light, int cascade)
{
	float3 fog = 0.0f;
	for (int i = 0; i < g_vl_steps; i++)
	{
		// Compute position in light space
		float4 pos_light = mul(float4(ray_pos, 1.0f), light_view_projection[cascade]);
		pos_light /= pos_light.w;	
		
		// Compute ray uv
		float2 ray_uv = pos_light.xy * float2(0.5f, -0.5f) + 0.5f;
		
		// Check to see if the light can "see" the pixel
		float depth_delta = light_depth_directional.SampleCmpLevelZero(sampler_cmp_depth, float3(ray_uv, cascade), pos_light.z).r;		
		if (depth_delta > 0.0f)
		{
			fog += ComputeScattering(ray_dot_light);
		}
		
		ray_pos += ray_step;
	}
	fog /= g_vl_steps;
	
	return fog;
}

float3 VolumetricLighting(Light light, float3 pos_world, float2 uv)
{
	float3 pixel_to_camera 			= g_camera_position.xyz - pos_world;
	float pixel_to_cameral_length 	= length(pixel_to_camera);
	float3 ray_dir					= pixel_to_camera / pixel_to_cameral_length;
	float step_length 				= pixel_to_cameral_length / g_vl_steps;
	float3 ray_step 				= ray_dir * step_length;
	float3 ray_pos 					= pos_world;
	float ray_dot_light				= dot(ray_dir, light.direction);
	float3 fog 						= 0.0f;

	// Apply dithering as it will allows us to get away with a crazy low sample count ;-)
	float3 dither_value = Dither(uv + g_taa_jitterOffset) * 300;
	ray_pos += ray_step * dither_value;
	
	// Find closest shadow cascade
	int cascade = 0;
	for (int cascade_index = 0; cascade_index < cascade_count; cascade_index++)
	{
		// Compute clip space position and uv for our ray
		float3 pos 	= mul(float4(ray_pos, 1.0f), light_view_projection[cascade_index]).xyz;
		float3 uv 	= pos * float3(0.5f, -0.5f, 0.5f) + 0.5f;	
		
		[branch]
		if (is_saturated(uv))
		{
			// Ray-march using the primary cascade
			float3 fog_primary 		= vl_raymarch(ray_pos, ray_step, ray_dot_light, cascade_index);
			float3 cascade_edge 	= (abs(pos) - 0.9f) * 2.5f;
			float cascade_lerp 		= max(cascade_edge.x, max(cascade_edge.y, cascade_edge.z));
			
			// If we are close to the edge of the primary cascade and a secondary cascade exists, lerp with it.
			[branch]
			if (cascade_lerp > 0.0f && cascade < cascade_count - 1)
			{
				// Ray-march using the secondary cascade
				int cacade_secondary = cascade + 1;
				float3 fog_secondary = vl_raymarch(ray_pos, ray_step, ray_dot_light, cacade_secondary);
				
				// Blend cascades	
				fog = lerp(fog_primary, fog_secondary, cascade_lerp);
			}
			else
			{
				fog = fog_primary;
			}
			
			break;
		}
	}
	
	return fog * light.color * light.intensity;
}