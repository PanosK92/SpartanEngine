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

static const float g_vl_steps 		= 64;
static const float g_vl_scattering 	= 0.97;
static const float g_vl_pow			= 0.5f;

// Mie scaterring approximated with Henyey-Greenstein phase function.
float ComputeScattering(float v_dot_l)
{
	float result = 1.0f - g_vl_scattering * g_vl_scattering;
	float e = abs(1.0f + g_vl_scattering * g_vl_scattering - (2.0f * g_vl_scattering) * v_dot_l);
	result /= pow(e, g_vl_pow);
	return result;
}

float3 VolumetricLighting(Light light, float3 pos_world, float2 uv)
{
	float3 pixel_to_camera 			= g_camera_position.xyz - pos_world;
	float pixel_to_cameral_length 	= length(pixel_to_camera);
	float3 ray_dir					= pixel_to_camera / pixel_to_cameral_length;
	float step_length 				= pixel_to_cameral_length / g_vl_steps;
	float3 ray_step 				= ray_dir * step_length;
	float3 ray_pos 					= pos_world;

	// Apply dithering as it will allows us to get away with a crazy low sample count ;-)
	float3 dither_value = Dither_Valve(uv * g_resolution) * 400;
	ray_pos += ray_step * dither_value;
	
	static const int cascade = 0;
	
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
			fog += ComputeScattering(dot(ray_dir, light.direction));
		}
		
		ray_pos += ray_step;
	}
	fog /= g_vl_steps;
	
	return fog * light.color * light.intensity;
}