float DepthMapLookup(Texture2D depthMap, SamplerState samplerState, float2 coords, float2 offset, float bias, float4 lightViewPosition)
{
	float2 texel = float2(1.0f / 1920, 1.0f / 1080);
	float shadowAmount = 0.0f;

	if ((saturate(coords).x == coords.x) && (saturate(coords).y == coords.y))
	{
		// Sample the shadow map depth value from the depth texture using the sampler at the projected texture coordinate location.
		float depthMapValue = depthMap.Sample(samplerState, coords + offset * texel).r;

		// Calculate the depth of the light.
		float pixelDepthValue = lightViewPosition.z / lightViewPosition.w;

		if (pixelDepthValue - bias > depthMapValue) // If the surface is behind the object, shadow it
			shadowAmount = 1.0f;
	}
	
	return shadowAmount;
}

float ShadowMapping(Texture2D depthMap, SamplerState samplerState, float4 view, float bias)
{
	float shadowFactor = 0.0f;

	// Calculate the projected texture coordinates.
	float2 projectDepthMapTexCoord;
	projectDepthMapTexCoord.x = view.x / view.w / 2.0f + 0.5f;
	projectDepthMapTexCoord.y = -view.y / view.w / 2.0f + 0.5f;

	return DepthMapLookup(depthMap, samplerState, projectDepthMapTexCoord, float2(0.0f, 0.0f), bias, view);
}

float ShadowMappingPCF(Texture2D depthMap, SamplerState samplerState, float4 view, float bias)
{
	float shadow = 0.0f;

	// Calculate the projected texture coordinates.
	float2 projectDepthMapTexCoord;
	projectDepthMapTexCoord.x = view.x / view.w / 2.0f + 0.5f;
	projectDepthMapTexCoord.y = -view.y / view.w / 2.0f + 0.5f;
 
	// 4x4 PCF
	for (float y = -1.5f; y <= 1.5f; y += 1.0f)
		for (float x = -1.5f; x <= 1.5f; x += 1.0f)
		{
			shadow += DepthMapLookup(depthMap, samplerState, projectDepthMapTexCoord, float2(x, y), bias, view);
		}

	return shadow / 16.0f;
}

float ShadowMapping(Texture2D depthMap, SamplerState samplerState, float4 lightPos, float bias, bool PCF)
{
	float shadow = 0.0f;
	if (PCF)
		shadow = ShadowMappingPCF(depthMap, samplerState, lightPos, bias);
	else
		shadow = ShadowMapping(depthMap, samplerState, lightPos, bias);
		
	return 1.0f - shadow;
}