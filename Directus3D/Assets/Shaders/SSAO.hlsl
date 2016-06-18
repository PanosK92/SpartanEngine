// Based on http://www.gamedev.net/page/resources/_/technical/graphics-programming-and-theory/a-simple-and-practical-approach-to-ssao-r2753 - José María Méndez

float2 g_screen_size = float2(1920.0f, 1080.0f);
float g_random_size = 64.0f;
float g_scale = 1.0f; // scales distance between occluders and occludee.
float g_bias = 0.0f; // controls the width of the occlusion cone considered by the occludee.
float g_sample_rad = 0.01f; // the sampling radius.
float g_intensity = 1.0f; // the ao intensity.

float3 GetPosition(float2 texCoord)
{
	return positionTexture.Sample(samplerPoint, texCoord).xyz;
}

float3 GetNormal(float2 texCoord)
{
	return normalize(UnpackNormal(normalTexture.Sample(samplerAniso, texCoord).xyz));
}

float3 GetRandomNormal(float2 texCoord)
{
	return normalize(UnpackNormal(noiseTexture.Sample(samplerAniso, g_screen_size * texCoord / g_random_size).xyz));
}

float AmbientOcclusion(float2 tcoord, float2 uv, float3 position, float3 normal)
{	
	float3 diff = GetPosition(tcoord + uv) - position;
	float3 v = (diff); // vector between occluder and occludee
	float d = length(diff) * g_scale; // distance between occluder and occludee
	
	// max(0.0, dot(N,V)) -> points directly above the occludee 
	// contribute more occlusion than points around it
	float occlusion = max(0.0, dot(normal, v) - g_bias);
	float attunation = 1.0f / (1.0f + d);
	
	return occlusion * attunation * g_intensity;
}

float SSAO(float2 texCoord)
{
	float3 position = GetPosition(texCoord);
	float3 normal = GetNormal(texCoord);
	float2 randomNormal = GetRandomNormal(texCoord).xy;
	
	const float2 vec[4] = {float2(1,0),float2(-1,0), float2(0,1),float2(0,-1)};
    float radius 		= g_sample_rad;

	int iterations = 4;
	float ao = 0.0f;
	for(int j = 0; j < iterations; j++)
    {
		float2 coord1 = reflect(vec[j], randomNormal) * radius; // original sampling coordinates, at 90º
		float2 coord2 = float2(coord1.x * 0.707f - coord1.y * 0.707f, coord1.x * 0.707f + coord1.y * 0.707f); // coord2 are the same coordinates, rotated 45º.
		
		ao += AmbientOcclusion(texCoord, coord1 * 0.25f, position, normal);
		ao += AmbientOcclusion(texCoord, coord2 * 0.5f, position, normal);
		ao += AmbientOcclusion(texCoord, coord1 * 0.75f, position, normal);
		ao += AmbientOcclusion(texCoord, coord2, position, normal);
    }
	
	ao /= (float)iterations * 4.0f;

	return 1.0f - ao;
}