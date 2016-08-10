float2 texOffset(float shadowMapSize, int u, int v)
{
    return float2(u * 1.0f / shadowMapSize, v * 1.0f / shadowMapSize);
}

float depthTest(Texture2D shadowMap, SamplerState samplerState, float2 uv, float compare)
{
    float depth = shadowMap.Sample(samplerState, uv).r;
    return step(compare, depth);
}

float sampleShadowMap(Texture2D shadowMap, SamplerState samplerState, float2 size, float2 uv, float compare)
{
    float2 texelSize = float2(1.0f, 1.0f) / size;
    float2 f = frac(uv * size + 0.5f);
    float2 centroidUV = floor(uv * size + 0.5f) / size;

    float lb = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(0.0f, 0.0f), compare);
    float lt = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(0.0f, 1.0f), compare);
    float rb = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(1.0f, 0.0f), compare);
    float rt = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(1.0f, 1.0f), compare);
    float a = lerp(lb, lt, f.y);
    float b = lerp(rb, rt, f.y);
    float c = lerp(a, b, f.x);
    return c;
}

float ShadowMappingPCF(Texture2D shadowMap, SamplerState samplerState, float4 pos, float bias)
{
	float shadowMapSize = 2048.0f; // temporary, yeap I don't like it either.
	
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

    // Perform PCF filtering on a 4 x 4 texel neighborhood
	float percentLit = 0.0f;
	for (float y = -1.5f; y <= 1.5f; ++y)
        for (float x = -1.5f; x <= 1.5f; ++x)
			percentLit += sampleShadowMap(shadowMap, samplerState, shadowMapSize, pos.xy + texOffset(shadowMapSize, x,y), pos.z);	
			
		
	return percentLit / 16.0f;
}

float random(float4 seed4) 
{
	float dot_product = dot(seed4, float4(12.9898f, 78.233f, 45.164f, 94.673f));
    return frac(sin(dot_product) * 43758.5453);
}

float ShadowMappingPoisson(Texture2D shadowMap, SamplerComparisonState samplerState, float4 pos, float bias)
{
	// Re-homogenize position after interpolation
    pos.xyz /= pos.w;
	
	// If position is not visible to the light - dont illuminate it
    // results in hard light frustum
    if( pos.x < -1.0f || pos.x > 1.0f ||
        pos.y < -1.0f || pos.y > 1.0f ||
        pos.z < 0.0f  || pos.z > 1.0f ) return 1.0f;

	// Transform clip space coords to texture space coords (-1:1 to 0:1)
	pos.x = pos.x / 2.0f + 0.5f;
	pos.y = pos.y / -2.0f + 0.5f;

	// Apply shadow map bias
    pos.z -= bias;

	 //Poisson sampling for shadow map
	float spread = 200.0f; // Defines how much the samples are “spread”
	float2 poissonDisk[4] = 
	{
	  float2( -0.94201624, -0.39906216 ),
	  float2( 0.94558609, -0.76890725 ),
	  float2( -0.094184101, -0.92938870 ),
	  float2( 0.34495938, 0.29387760 )
	};

	float sum = 0;
	for (int i= 0; i < 4; i++)
	{
		int index = int(16.0f * random(pos * i)) % 16; // A random number between 0 and 15, different for each pixel (and each i !)
		sum += shadowMap.SampleCmpLevelZero(samplerState, pos.xy + poissonDisk[index], pos.z);
	}
	float shadowAmount = sum / 4.0f;
	
	return shadowAmount;
}