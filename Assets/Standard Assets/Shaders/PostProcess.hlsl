// = INCLUDES ========
#include "Common.hlsl"
//====================

Texture2D sourceTexture 		: register(t0);
Texture2D sourceTexture2 		: register(t1);
SamplerState bilinearSampler 	: register(s0);

cbuffer DefaultBuffer
{
    matrix mTransform;
    float2 resolution;
    float2 computeLuma;
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
								[Pixel Shader]
------------------------------------------------------------------------------*/
float4 DirectusPixelShader(VS_Output input) : SV_TARGET
{
    float2 texCoord 	= input.uv;
    float4 color 		= float4(0.0f, 0.0f, 0.0f, 1.0f);
    float2 texelSize 	= float2(1.0f / resolution.x, 1.0f / resolution.y);
	
#if PASS_FXAA
	color.rgb 	= FXAA(texCoord, texelSize, sourceTexture, bilinearSampler);
	color.a 	= 1.0f;
#endif

#if PASS_CHROMATIC_ABERRATION
	color.rgb = ChromaticAberrationPass(texCoord, texelSize, sourceTexture, bilinearSampler);
#endif

#if PASS_SHARPENING
	color.rgb 	= LumaSharpen(texCoord, sourceTexture, bilinearSampler, resolution);	
#endif
	
#if PASS_BLUR_BOX
	color = Pass_BlurBox(texCoord, texelSize, 4, sourceTexture, bilinearSampler);
#endif

#if PASS_BLUR_GAUSSIAN_H
	color = Pass_BlurGaussian(texCoord, sourceTexture, bilinearSampler, resolution, float2(1.0f, 0.0f), 3.0f);
#endif

#if PASS_BLUR_GAUSSIAN_V
	color = Pass_BlurGaussian(texCoord, sourceTexture, bilinearSampler, resolution, float2(0.0f, 1.0f), 3.0f);
#endif

#if PASS_BRIGHT
	color 				= sourceTexture.Sample(bilinearSampler, input.uv);
    float luminance 	= dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));	
	color 				= luminance > 1.0f ? color : float4(0.0f, 0.0f, 0.0f, computeLuma.x == 1.0f ? color.a : 1.0f); // maintain alpha as it holds FXAA luma
#endif

#if PASS_BLEND_ADDITIVE
	float4 sourceColor 	= sourceTexture.Sample(bilinearSampler, input.uv);
	float4 sourceColor2 = sourceTexture2.Sample(bilinearSampler, input.uv);
	color 				= sourceColor + sourceColor2;
#endif

#if PASS_CORRECTION
	color 		= sourceTexture.Sample(bilinearSampler, texCoord);
	color.rgb 	= ACESFitted(color);
	color 		= ToGamma(color);
#endif

    return color;
}