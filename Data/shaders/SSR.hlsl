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

static const int g_steps 					= 16;
static const int g_binarySearchSteps 		= 8;
static const float g_binarySearchThreshold 	= 0.01f;

float2 SSR_BinarySearch(float3 ray_dir, inout float3 ray_pos, Texture2D tex_depth, SamplerState sampler_point_clamp)
{
	for (int i = 0; i < g_binarySearchSteps; i++)
	{	
		float2 ray_uv 		= project(ray_pos, g_projection);
		float depth 		= get_linear_depth(tex_depth, sampler_point_clamp, ray_uv);
		float depth_delta 	= ray_pos.z - depth;

		if (depth_delta <= 0.0f)
			ray_pos += ray_dir;

		ray_dir *= 0.5f;
		ray_pos -= ray_dir;
	}

	float2 ray_uv 		= project(ray_pos, g_projection);
	float depth_sample 	= get_linear_depth(tex_depth, sampler_point_clamp, ray_uv);
	float depth_delta 	= ray_pos.z - depth_sample;

	return abs(depth_delta) < g_binarySearchThreshold ? project(ray_pos, g_projection) : 0.0f;
}

float2 SSR_RayMarch(float3 ray_pos, float3 ray_dir, Texture2D tex_depth, SamplerState sampler_point_clamp)
{
	for(int i = 0; i < g_steps; i++)
	{
		// Step ray
		ray_pos 		+= ray_dir;
		float2 ray_uv 	= project(ray_pos, g_projection);

		// Compute depth
		float depth_current = ray_pos.z;
		float depth_sampled = get_linear_depth(tex_depth, sampler_point_clamp, ray_uv);
		float depth_delta 	= ray_pos.z - depth_sampled;
		
		[branch]
		if (depth_delta > 0.0f)
			return SSR_BinarySearch(ray_dir, ray_pos, tex_depth, sampler_point_clamp);
	}

	return 0.0f;
}

float3 SSR(float3 position, float3 normal, float2 uv, float roughness, Texture2D tex_color, Texture2D tex_depth, SamplerState sampler_point_clamp)
{
	float noise_scale = 0.01f; // scale the noise down a bit for now as this using jitter needs a blur pass as well (for acceptable results)
	float3 jitter = float(randomize(uv) * 2.0f - 1.0f) * roughness * noise_scale;

	// Convert everything to view space
	float3 viewPos		= mul(float4(position, 1.0f), g_view).xyz;
	float3 viewNormal	= normalize(mul(float4(normal, 0.0f), g_view).xyz);
	float3 viewRayDir 	= normalize(reflect(viewPos, viewNormal) + jitter);
	
	float3 ray_pos 			= viewPos;
	float2 reflection_uv 	= SSR_RayMarch(ray_pos, viewRayDir, tex_depth, sampler_point_clamp);
	float2 edgeFactor 		= float2(1, 1) - pow(saturate(abs(reflection_uv - float2(0.5f, 0.5f)) * 2), 8);
	float screenEdge 		= saturate(min(edgeFactor.x, edgeFactor.y));
	
	return tex_color.Sample(sampler_point_clamp, reflection_uv).rgb * screenEdge;
}