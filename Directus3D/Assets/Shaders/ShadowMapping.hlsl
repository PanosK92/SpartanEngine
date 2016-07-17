float ShadowMappingPCF(Texture2D shadowMap, SamplerComparisonState samplerState, float4 pos, float bias, float2 texel)
{
	float ambient = 0.2f;
	
	//re-homogenize position after interpolation
    pos.xyz /= pos.w;
	
	//if position is not visible to the light - dont illuminate it
    //results in hard light frustum
    if( pos.x < -1.0f || pos.x > 1.0f ||
        pos.y < -1.0f || pos.y > 1.0f ||
        pos.z < 0.0f  || pos.z > 1.0f ) return ambient;

	//transform clip space coords to texture space coords (-1:1 to 0:1)
	pos.x = pos.x / 2.0f + 0.5f;
	pos.y = pos.y / -2.0f + 0.5f;

	//apply shadow map bias
    pos.z -= bias;

	float2 xy = float2(pos.x, pos.y);
	float2 z = pos.z;
	
	 //PCF sampling for shadow map
    float sum = 0;
    float x, y;

    //perform PCF filtering on a 4 x 4 texel neighborhood
    for (y = -1.5f; y <= 1.5f; y += 1.0f)
    {
        for (x = -1.5; x <= 1.5f; x += 1.0f)
        {
            sum += shadowMap.SampleCmpLevelZero(samplerState, xy, z);
        }
    }
	float shadowAmount = sum / 16.0f;
	
	return shadowAmount;
}

float random(float4 seed4) 
{
	float dot_product = dot(seed4, float4(12.9898f, 78.233f, 45.164f, 94.673f));
    return frac(sin(dot_product) * 43758.5453);
}

float ShadowMappingPoisson(Texture2D shadowMap, SamplerComparisonState samplerState, float4 pos, float bias, float2 texel)
{
	float ambient = 0.2f;
	
	//re-homogenize position after interpolation
    pos.xyz /= pos.w;
	
	//if position is not visible to the light - dont illuminate it
    //results in hard light frustum
    if( pos.x < -1.0f || pos.x > 1.0f ||
        pos.y < -1.0f || pos.y > 1.0f ||
        pos.z < 0.0f  || pos.z > 1.0f ) return ambient;

	//transform clip space coords to texture space coords (-1:1 to 0:1)
	pos.x = pos.x / 2.0f + 0.5f;
	pos.y = pos.y / -2.0f + 0.5f;

	//apply shadow map bias
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