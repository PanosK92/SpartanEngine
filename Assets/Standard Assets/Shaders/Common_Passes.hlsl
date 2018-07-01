//= DEFINES ===================
#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_QUALITY__PRESET 29
//=============================

// = INCLUDES =============
#include "FXAA.hlsl"
#include "LumaSharpen.hlsl"
//=========================

/*------------------------------------------------------------------------------
									[FXAA]
------------------------------------------------------------------------------*/
float3 Pass_FXAA(float2 texCoord, float2 texelSize, Texture2D sourceTexture, SamplerState bilinearSampler)
{
	FxaaTex tex 						= { bilinearSampler, sourceTexture };	
    float2 fxaaQualityRcpFrame			= texelSize;
    float fxaaQualitySubpix				= 1.5f; 	// 0.75f	// The amount of sub-pixel aliasing removal.
    float fxaaQualityEdgeThreshold		= 0.125f; 	// 0.125f	// Edge detection threshold. The minimum amount of local contrast required to apply algorithm.
    float fxaaQualityEdgeThresholdMin	= 0.0833f; 	// 0.0833f	// Darkness threshold. Trims the algorithm from processing darks.
	
	float3 fxaa = FxaaPixelShader
	( 
		texCoord, 0, tex, tex, tex,
		fxaaQualityRcpFrame, 0, 0, 0,
		fxaaQualitySubpix,
		fxaaQualityEdgeThreshold,
		fxaaQualityEdgeThresholdMin,
		0, 0, 0, 0
	).xyz;
	
    return fxaa;
}

/*------------------------------------------------------------------------------
							[SHARPENING]
------------------------------------------------------------------------------*/
float4 Pass_Sharpening(float2 texCoord, Texture2D sourceTexture, SamplerState bilinearSampler, float resolution)
{
	return LumaSharpen(sourceTexture, bilinearSampler, texCoord, resolution);
}

/*------------------------------------------------------------------------------
								[BLUR]
------------------------------------------------------------------------------*/
float4 Pass_BlurBox(float2 texCoord, float2 texelSize, int blurSize, Texture2D sourceTexture, SamplerState pointSampler)
{
	float4 result 	= float4(0.0f, 0.0f, 0.0f, 0.0f);
	float temp 		= float(-blurSize) * 0.5f + 0.5f;
	float2 hlim 	= float2(temp, temp);
	for (int i = 0; i < blurSize; ++i)
	{
		for (int j = 0; j < blurSize; ++j) 
		{
			float2 offset = (hlim + float2(float(i), float(j))) * texelSize;
			result += sourceTexture.Sample(pointSampler, texCoord + offset);
		}
	}
		
	result = result / float(blurSize * blurSize);
	   
	return result;
}

// Calculates the gaussian blur weight for a given distance and sigmas
float CalcGaussianWeight(int sampleDist, float sigma)
{
    float g = 1.0f / sqrt(2.0f * 3.14159 * sigma * sigma);
    return (g * exp(-(sampleDist * sampleDist) / (2 * sigma * sigma)));
}

// Performs a gaussian blur in one direction
float4 Pass_BlurGaussian(float2 uv, Texture2D sourceTexture, SamplerState pointSampler, float2 resolution, float2 direction, float sigma)
{
	// https://github.com/TheRealMJP/MSAAFilter/blob/master/MSAAFilter/PostProcessing.hlsl#L50
	float weightSum = 0.0f;
    float4 color = 0;
    for (int i = -10; i < 10; i++)
    {
        float weight = CalcGaussianWeight(i, sigma);
        weightSum += weight;
        float2 texCoord = uv;
        texCoord += (i / resolution) * direction;
        float4 sample = sourceTexture.Sample(pointSampler, texCoord);
        color += sample * weight;
    }

    color /= weightSum;

	return color;
}	

/*------------------------------------------------------------------------------
								[CORRECTION]
------------------------------------------------------------------------------*/
float4 Pass_Correction(float2 texCoord, Texture2D sourceTexture, SamplerState pointSampler)
{
	float4 color 	= sourceTexture.Sample(pointSampler, texCoord);
	color.rgb 		= ACESFilm(color).rgb; 								// ACES Filmic Tone Mapping (default tone mapping curve in Unreal Engine 4)
    color 			= ToGamma(color); 									// gamma correction
    color.a 		= dot(color.rgb, float3(0.299f, 0.587f, 0.114f)); 	// compute luma as alpha for FXAA
	   
	return color;
}