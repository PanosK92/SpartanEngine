float4 Blur_Box(float2 texCoord, float2 texelSize, int blurSize, Texture2D sourceTexture, SamplerState bilinearSampler)
{
	float4 result 	= float4(0.0f, 0.0f, 0.0f, 0.0f);
	float temp 		= float(-blurSize) * 0.5f + 0.5f;
	float2 hlim 	= float2(temp, temp);
	for (int i = 0; i < blurSize; ++i)
	{
		for (int j = 0; j < blurSize; ++j) 
		{
			float2 offset = (hlim + float2(float(i), float(j))) * texelSize;
			result += sourceTexture.SampleLevel(bilinearSampler, texCoord + offset, 0);
		}
	}
		
	result = result / float(blurSize * blurSize);
	   
	return result;
}

// Calculates the gaussian blur weight for a given distance and sigmas
float CalcGaussianWeight(int sampleDist, float sigma)
{
    float g = 1.0f / sqrt(2.0f * 3.14159f * sigma * sigma);
    return (g * exp(-(sampleDist * sampleDist) / (2.0f * sigma * sigma)));
}

// Performs a gaussian blur in one direction
float4 Blur_Gaussian(float2 uv, Texture2D sourceTexture, SamplerState bilinearSampler, float2 resolution, float2 direction, float sigma)
{
	// https://github.com/TheRealMJP/MSAAFilter/blob/master/MSAAFilter/PostProcessing.hlsl#L50
	float weightSum = 0.0f;
    float4 color 	= 0;
    for (int i = -7; i < 7; i++)
    {
        float weight 	= CalcGaussianWeight(i, sigma);
        weightSum 		+= weight;
        float2 texCoord = uv;
        texCoord 		+= (i / resolution) * direction;
        float4 sample 	= sourceTexture.SampleLevel(bilinearSampler, texCoord, 0);
        color 			+= sample * weight;
    }

    color /= weightSum;

	return color;
}

// Performs a bilateral gaussian blur (depth aware) in one direction
float4 Blur_GaussianBilateral(float2 uv, Texture2D sourceTexture, Texture2D depthTexture, SamplerState bilinearSampler, float2 resolution, float2 direction, float sigma)
{
	float weightSum 	= 0.0f;
    float4 color 		= 0;
	float origin_depth	= depthTexture.SampleLevel(bilinearSampler, uv, 0).r;
	float threshold		= 0.00005f;
	
    for (int i = -7; i < 7; i++)
    {
		float2 texCoord 	= uv;	
        texCoord 			+= (i / resolution) * direction;    
		float sampleDepth 	= depthTexture.SampleLevel(bilinearSampler, texCoord, 0).r;
		float depthDelta	= abs(origin_depth - sampleDepth);
		if (depthDelta < threshold)
		{
			float weight 		= CalcGaussianWeight(i, sigma);
			float4 sample 		= sourceTexture.SampleLevel(bilinearSampler, texCoord, 0);
			weightSum 			+= weight; 
			color 				+= sample * weight;
		}
    }

    color /= weightSum;

	return color;
}