//= DEFINES ===================
#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_QUALITY__PRESET 29
//=============================

// = INCLUDES =============
#include "Common.hlsl"
#include "FXAA.hlsl"
#include "LumaSharpen.hlsl"
#include "ACES.hlsl"
//=========================

Texture2D sourceTexture 		: register(t0);
Texture2D sourceTexture2 		: register(t1);
SamplerState pointSampler 		: register(s0);
SamplerState bilinearSampler 	: register(s1);

cbuffer DefaultBuffer
{
    matrix mTransform;
    float2 resolution;
    float2 padding;
};

struct VS_Output
{
    float4 position : SV_POSITION;
    float2 uv 		: TEXCOORD;
};

// Vertex Shader
VS_Output DirectusVertexShader(Vertex_PosUv input)
{
    VS_Output output;
	
    input.position.w = 1.0f;
    output.position = mul(input.position, mTransform);
    output.uv = input.uv;
	
    return output;
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

// Pixel Shader
float4 DirectusPixelShader(VS_Output input) : SV_TARGET
{
    float2 texCoord 	= input.uv;
    float4 color 		= float4(0.0f, 0.0f, 0.0f, 1.0f);
    float2 texelSize 	= float2(1.0f / resolution.x, 1.0f / resolution.y);
	
#if PASS_FXAA
	FxaaTex tex 						= { bilinearSampler, sourceTexture };	
    float2 fxaaQualityRcpFrame			= texelSize;
    float fxaaQualitySubpix				= 1.5f; 	// 0.75f	// The amount of sub-pixel aliasing removal.
    float fxaaQualityEdgeThreshold		= 0.125f; 	// 0.125f	// Edge detection threshold. The minimum amount of local contrast required to apply algorithm.
    float fxaaQualityEdgeThresholdMin	= 0.0833f; 	// 0.0833f	// Darkness threshold. Trims the algorithm from processing darks.
	
	color = FxaaPixelShader
	( 
		texCoord, 0, tex, tex, tex,
		fxaaQualityRcpFrame, 0, 0, 0,
		fxaaQualitySubpix,
		fxaaQualityEdgeThreshold,
		fxaaQualityEdgeThresholdMin,
		0, 0, 0, 0
	);
#endif
	
#if PASS_SHARPENING
	color = LumaSharpen(sourceTexture, bilinearSampler, texCoord, resolution);
	color.a = 1.0f;
#endif
	
#if PASS_BLUR_BOX
	color = Pass_BlurBox(texCoord, texelSize, 4, sourceTexture, pointSampler);
#endif

#if PASS_BLUR_GAUSSIAN_H
	color = Pass_BlurGaussian(texCoord, sourceTexture, pointSampler, resolution, float2(1.0f, 0.0f), 3.0f);
#endif

#if PASS_BLUR_GAUSSIAN_V
	color = Pass_BlurGaussian(texCoord, sourceTexture, pointSampler, resolution, float2(0.0f, 1.0f), 3.0f);
#endif

#if PASS_BRIGHT
	color 				= sourceTexture.Sample(pointSampler, input.uv);
    float luminance 	= dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));	
	color 				= luminance > 1.0f ? color : float4(0.0f, 0.0f, 0.0f, 1.0f);
#endif

#if PASS_BLEND_ADDITIVE
	float4 sourceColor 	= sourceTexture.Sample(pointSampler, input.uv);
	float4 sourceColor2 = sourceTexture2.Sample(pointSampler, input.uv);
	color 				= sourceColor + sourceColor2;
#endif

#if PASS_CORRECTION
	color 		= sourceTexture.Sample(pointSampler, texCoord);
	color.rgb 	= ACESFitted(color);
	color 		= ToGamma(color);
#endif

    return color;
}