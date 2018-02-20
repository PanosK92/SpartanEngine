//= DEFINES ======================
#define PCF 2
#define UnrollPCF (PCF * 2.0f) + 1
//================================

float2 texOffset(float2 shadowMapSize, int x, int y)
{
    return float2(x * 1.0f / shadowMapSize.x, y * 1.0f / shadowMapSize.y);
}

float depthTest(Texture2D shadowMap, SamplerState samplerState, float2 texCoords, float compare)
{
    float depth = shadowMap.Sample(samplerState, texCoords).r;
    return step(compare, depth);
}

float sampleShadowMap(Texture2D shadowMap, SamplerState samplerState, float2 size, float2 texCoords, float compare)
{
    float2 texelSize = float2(1.0f, 1.0f) / size;
    float2 f = frac(texCoords * size + 0.5f);
    float2 centroidUV = floor(texCoords * size + 0.5f) / size;

    float lb = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(0.0f, 0.0f), compare);
    float lt = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(0.0f, 1.0f), compare);
    float rb = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(1.0f, 0.0f), compare);
    float rt = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(1.0f, 1.0f), compare);
    float a = lerp(lb, lt, f.y);
    float b = lerp(rb, rt, f.y);
    float c = lerp(a, b, f.x);
	
    return c;
}

// Performs PCF filtering on a 4 x 4 texel neighborhood
float sampleShadowMapPCF(Texture2D shadowMap, SamplerState samplerState, float2 size, float2 texCoords, float compare)
{
	float amountLit = 0.0f;
	float count = 0.0f;
	[unroll(UnrollPCF)]
	for (float y = -PCF; y <= PCF; ++y)
	{
		[unroll(UnrollPCF)]
		for (float x = -PCF; x <= PCF; ++x)
		{
			amountLit += sampleShadowMap(shadowMap, samplerState, size, texCoords + texOffset(size, x, y), compare);
			count++;			
		}
	}
	return amountLit /= count;
}

float random(float2 seed2) 
{
	float4 seed4 = float4(seed2.x, seed2.y, seed2.y, 1.0f);
	float dot_product = dot(seed4, float4(12.9898f, 78.233f, 45.164f, 94.673f));
    return frac(sin(dot_product) * 43758.5453);
}

float ShadowMapping(Texture2D shadowMap, SamplerState samplerState, float shadowMapResolution, float4 pos, float3 normal, float3 lightDir, float bias)
{	
	// Re-homogenize position after interpolation
    pos.xyz /= pos.w;
	
	// If position is not visible to the light, dont illuminate it
    if( pos.x < -1.0f || pos.x > 1.0f ||
        pos.y < -1.0f || pos.y > 1.0f ||
        pos.z < 0.0f  || pos.z > 1.0f ) return 1.0f;

	// Transform clip space coords to texture space coords (-1:1 to 0:1)
	pos.x = pos.x / 2.0f + 0.5f;
	pos.y = pos.y / -2.0f + 0.5f;

	// Apply shadow map bias
	pos.z -= bias;
	
	float amountLit = 0.0f;
	
	float shadowMappingQuality = 0.5f;
	
	// Hard shadows (0.5f) --> PCF + Interpolation
	if (shadowMappingQuality == 0.5f)
	{
		// Perform PCF filtering on a 4 x 4 texel neighborhood
		amountLit = sampleShadowMapPCF(shadowMap, samplerState, shadowMapResolution, pos.xy, pos.z);
	}
	// Soft shadows (1.0f) --> Interpolation + Stratified Poisson Sampling
	else
	{
		// Poisson sampling for shadow map
		float packing = 4000.0f; // how close together are the samples
		float2 poissonDisk[4] = 
		{
		  float2( -0.94201624f, -0.39906216f ),
		  float2( 0.94558609f, -0.76890725f ),
		  float2( -0.094184101f, -0.92938870f ),
		  float2( 0.34495938f, 0.29387760f )
		};

		uint samples = 4;
		[unroll(samples)]
		for (uint i = 0; i < samples; i++)
		{
			uint index = uint(samples * random(pos.xy * i)) % samples; // A pseudo-random number between 0 and 15, different for each pixel and each index
			amountLit += sampleShadowMap(shadowMap, samplerState, shadowMapResolution, pos.xy + (poissonDisk[index] / packing), pos.z);
		}	
		amountLit /= (float)samples;
	}
	
	return amountLit;
}