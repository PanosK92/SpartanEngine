static const int g_steps 				= 16;
static const int g_binarySearchSteps 	= 16;
static const float g_ray_step 			= 1.15f;

float2 Project(float3 viewPosition, matrix projection)
{
	float4 projectedCoords 	= mul(float4(viewPosition, 1.0f), projection);
	projectedCoords.xy 		/= projectedCoords.w;
	projectedCoords.xy 		= projectedCoords.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

	return projectedCoords.xy;
}

float GetLinearDepth(Texture2D tex_depth, SamplerState samplerLinear, float2 uv, float farPlane)
{
	return tex_depth.Sample(samplerLinear, uv.xy).r * farPlane;
}

float2 SSR_BinarySearch(float3 ray_dir, inout float3 ray_pos, matrix projection, Texture2D tex_depth, SamplerState samplerLinear)
{
	for (int i = 0; i < g_binarySearchSteps; i++)
	{	
		float2 ray_uv 		= Project(ray_pos, projection);
		float depth 		= GetLinearDepth(tex_depth, samplerLinear, ray_uv, farPlane);
		float depth_delta 	= ray_pos.z - depth;

		if (depth_delta <= 0.0f)
			ray_pos += ray_dir;

		ray_dir *= 0.5f;
		ray_pos -= ray_dir;
	}

	return Project(ray_pos, projection);
}

float2 SSR_RayMarch(float3 ray_pos, float3 ray_dir, float farPlane, matrix projection, Texture2D tex_depth, SamplerState samplerLinear)
{
	for(int i = 0; i < g_steps; i++)
	{
		// Step ray
		ray_pos 		+= ray_dir;
		float2 ray_uv 	= Project(ray_pos, projection);

		// Compute depth
		float depth_current = ray_pos.z;
		float depth_sampled = GetLinearDepth(tex_depth, samplerLinear, ray_uv, farPlane);
		float depth_delta 	= ray_pos.z - depth_sampled;
		
		[branch]
		if (depth_delta > 0.0f)
			return SSR_BinarySearch(ray_dir, ray_pos, projection, tex_depth, samplerLinear);

		ray_dir *= g_ray_step;
	}

	return float2(0.0f, 0.0f);
}

float3 SSR(float3 position, float3 normal, float3 camera_to_pixel, float farPlane, matrix view, matrix projection, Texture2D tex_albedo, Texture2D tex_depth, SamplerState samplerLinear)
{
	// Convert everything to view space
	float3 viewPos				= mul(float4(position, 1.0f), view).xyz;
	float3 viewNormal			= mul(float4(normal, 0.0f), view).xyz;
	float3 viewRayDir 			= normalize(reflect(viewPos, viewNormal));
	
	float3 ray_pos 			= viewPos;
	float2 reflection_uv 	= SSR_RayMarch(ray_pos, viewRayDir, farPlane, projection, tex_depth, samplerLinear);
	float2 edgeFactor 		= float2(1, 1) - pow(saturate(abs(reflection_uv.xy - float2(0.5f, 0.5f)) * 2), 8);
	float screenEdge 		= saturate(min(edgeFactor.x, edgeFactor.y));
	
	if (reflection_uv.x * reflection_uv.y == 0.0f)
		return 0.0f;

	return tex_albedo.Sample(samplerLinear, reflection_uv).rgb * screenEdge;
}