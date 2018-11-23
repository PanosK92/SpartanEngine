// = INCLUDES ========
#include "Common.hlsl"
//====================

Texture2D sourceTexture 		: register(t0);
Texture2D sourceTexture2 		: register(t1);
SamplerState samplerState 		: register(s0);

cbuffer DefaultBuffer
{
    matrix mTransform;
    float2 texRes;
    float2 parameters;
};

struct VS_Output
{
    float4 position : SV_POSITION;
    float2 uv 		: TEXCOORD;
};

// Vertex Shader
VS_Output mainVS(Vertex_PosUv input)
{
    VS_Output output;
	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mTransform);
    output.uv 			= input.uv;
	
    return output;
}

/*------------------------------------------------------------------------------
								[Pixel Shader]
------------------------------------------------------------------------------*/
float4 mainPS(VS_Output input) : SV_TARGET
{
    float2 texCoord 	= input.uv;
    float4 color 		= float4(0.0f, 0.0f, 0.0f, 1.0f);
    float2 texelSize 	= float2(1.0f / texRes.x, 1.0f / texRes.y);
	
#if PASS_FXAA
	color.rgb 	= FXAA(texCoord, texelSize, sourceTexture, samplerState);
	color.a 	= 1.0f;
#endif

#if PASS_CHROMATIC_ABERRATION
	color.rgb = ChromaticAberrationPass(texCoord, texelSize, sourceTexture, samplerState);
#endif

#if PASS_SHARPENING
	color.rgb 	= LumaSharpen(texCoord, sourceTexture, samplerState, texRes);	
#endif
	
#if PASS_BLUR_BOX
	float blurAmount = parameters.x;
	color = Pass_BlurBox(texCoord, texelSize, blurAmount, sourceTexture, samplerState);
#endif

#if PASS_BLUR_GAUSSIAN_H
	float2 dir 	= float2(1.0f, 0.0f);
	float sigma = parameters.x;
	color 		= Pass_BlurGaussian(texCoord, sourceTexture, samplerState, texRes, dir, sigma);
#endif

#if PASS_BLUR_GAUSSIAN_V
	float2 dir 	= float2(0.0f, 1.0f);
	float sigma = parameters.x;
	color 		= Pass_BlurGaussian(texCoord, sourceTexture, samplerState, texRes, dir, sigma);
#endif

#if PASS_BLUR_BILATERAL_GAUSSIAN_H
	float sigma 		= parameters.x;
	float pixelStride 	= parameters.y;
	float2 dir 			= float2(1.0f, 0.0f) * pixelStride;
	color 		= Pass_BilateralGaussian(texCoord, sourceTexture, sourceTexture2, samplerState, texRes, dir, sigma);
#endif

#if PASS_BLUR_BILATERAL_GAUSSIAN_V
	float sigma 		= parameters.x;
	float pixelStride 	= parameters.y;
	float2 dir 			= float2(0.0f, 1.0f) * pixelStride;
	color 				= Pass_BilateralGaussian(texCoord, sourceTexture, sourceTexture2, samplerState, texRes, dir, sigma);
#endif

#if PASS_BRIGHT
	color 				= sourceTexture.Sample(samplerState, input.uv);
    float luminance 	= dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));	
	color 				= luminance > 1.0f ? color : float4(0.0f, 0.0f, 0.0f, color.a);
#endif

#if PASS_BLEND_ADDITIVE
	float4 sourceColor 	= sourceTexture.Sample(samplerState, input.uv);
	float4 sourceColor2 = sourceTexture2.Sample(samplerState, input.uv);
	color 				= sourceColor + sourceColor2 * parameters.x;
#endif

#if PASS_CORRECTION
	color 		= sourceTexture.Sample(samplerState, texCoord);
	color.rgb 	= ACESFitted(color.rgb);
	color 		= ToGamma(color);
#endif

    return color;
}