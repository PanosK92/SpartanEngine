// = INCLUDES =====================
#include "Common.hlsl"
#include "Vertex.hlsl"
#include "Buffer.hlsl"
#include "Sharpening.hlsl"
#include "ChromaticAberration.hlsl"
#include "Blur.hlsl"
#include "ACES.hlsl"
#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_QUALITY__PRESET 39
#include "FXAA.hlsl"
//=================================

Texture2D sourceTexture 		: register(t0);
Texture2D sourceTexture2 		: register(t1);
SamplerState samplerState 		: register(s0);

struct VS_Output
{
    float4 position : SV_POSITION;
    float2 uv 		: TEXCOORD;
};

VS_Output mainVS(Vertex_PosUv input)
{
    VS_Output output;
	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mMVP);
    output.uv 			= input.uv;
	
    return output;
}

float4 mainPS(VS_Output input) : SV_TARGET
{
    float2 texCoord 	= input.uv;
    float4 color 		= float4(0.0f, 0.0f, 0.0f, 1.0f);

#if PASS_FXAA
	// Requirements: Bilinear sampler
	FxaaTex tex 				= { samplerState, sourceTexture };
    float2 fxaaQualityRcpFrame	= texelSize;
  
	color.rgb = FxaaPixelShader
	( 
		texCoord, 0, tex, tex, tex,
		fxaaQualityRcpFrame, 0, 0, 0,
		fxaa_subPix,
		fxaa_edgeThreshold,
		fxaa_edgeThresholdMin,
		0, 0, 0, 0
	).rgb;
	color.a = 1.0f;
#endif

#if PASS_CHROMATIC_ABERRATION
	// Requirements: Bilinear sampler
	color.rgb = ChromaticAberration(texCoord, texelSize, sourceTexture, samplerState);
#endif

#if PASS_SHARPENING
	// Requirements: Bilinear sampler
	color.rgb = LumaSharpen(texCoord, sourceTexture, samplerState, resolution, sharpen_strength, sharpen_clamp);	
#endif
	
#if PASS_BLUR_BOX
	color = Blur_Box(texCoord, texelSize, blur_sigma, sourceTexture, samplerState);
#endif

#if PASS_BLUR_GAUSSIAN
	// Requirements: Bilinear sampler
	color = Blur_Gaussian(texCoord, sourceTexture, samplerState, resolution, blur_direction, blur_sigma);
#endif

#if PASS_BLUR_BILATERAL_GAUSSIAN
	// Requirements: Bilinear sampler
	color = Blur_GaussianBilateral(texCoord, sourceTexture, sourceTexture2, samplerState, resolution, blur_direction, blur_sigma);
#endif

#if PASS_BRIGHT
	color 				= sourceTexture.Sample(samplerState, texCoord);
    float luminance 	= dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));	
	color 				= luminance > 1.0f ? color : float4(0.0f, 0.0f, 0.0f, color.a);
#endif

#if PASS_BLEND_ADDITIVE
	float4 sourceColor 	= sourceTexture.Sample(samplerState, texCoord);
	float4 sourceColor2 = sourceTexture2.Sample(samplerState, texCoord);
	color 				= sourceColor + sourceColor2 * bloom_intensity;
#endif

#if PASS_LUMA
	// Computes luma for FXAA
	color 		= sourceTexture.Sample(samplerState, texCoord);
    color.a 	= dot(color.rgb, float3(0.299f, 0.587f, 0.114f));
#endif

#if PASS_CORRECTION
	color 		= sourceTexture.Sample(samplerState, texCoord);
	color.rgb 	= ACESFitted(color.rgb);
	color 		= ToGamma(color);
#endif

    return color;
}