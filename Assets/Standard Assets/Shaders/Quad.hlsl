// = INCLUDES =====================
#include "Common.hlsl"
#include "Vertex.hlsl"
#include "Sharpening.hlsl"
#include "ChromaticAberration.hlsl"
#include "Blur.hlsl"
#include "ToneMapping.hlsl"
#include "ResolveTAA.hlsl"
#include "MotionBlur.hlsl"
#include "Dithering.hlsl"
#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_QUALITY__PRESET 39
#include "FXAA.hlsl"
//=================================

Texture2D sourceTexture 		: register(t0);
Texture2D sourceTexture2 		: register(t1);
Texture2D sourceTexture3 		: register(t2);
Texture2D sourceTexture4 		: register(t3);
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
    output.position 	= mul(input.position, g_viewProjectionOrtho);
    output.uv 			= input.uv;
	
    return output;
}

float4 mainPS(VS_Output input) : SV_TARGET
{
    float2 texCoord 	= input.uv;
    float4 color 		= float4(1.0f, 0.0f, 0.0f, 1.0f);

#if PASS_GAMMA_CORRECTION
	color 		= sourceTexture.Sample(samplerState, texCoord);
	color 		= Gamma(color);
#endif

#if PASS_TONEMAPPING
	color 		= sourceTexture.Sample(samplerState, texCoord);
	color.rgb 	= ToneMap(color.rgb);
#endif

#if PASS_TEXTURE
	color = sourceTexture.Sample(samplerState, texCoord);
#endif

#if PASS_FXAA
	// Requirements: Bilinear sampler
	FxaaTex tex 				= { samplerState, sourceTexture };
    float2 fxaaQualityRcpFrame	= g_texelSize;
  
	color.rgb = FxaaPixelShader
	( 
		texCoord, 0, tex, tex, tex,
		fxaaQualityRcpFrame, 0, 0, 0,
		g_fxaa_subPix,
		g_fxaa_edgeThreshold,
		g_fxaa_edgeThresholdMin,
		0, 0, 0, 0
	).rgb;
	color.a = 1.0f;
#endif

#if PASS_CHROMATIC_ABERRATION
	// Requirements: Bilinear sampler
	color.rgb = ChromaticAberration(texCoord, g_texelSize, sourceTexture, samplerState);
#endif

#if PASS_SHARPENING
	// Requirements: Bilinear sampler
	color.rgb = LumaSharpen(texCoord, sourceTexture, samplerState, g_resolution, g_sharpen_strength, g_sharpen_clamp);	
#endif
	
#if PASS_BLUR_BOX
	color = Blur_Box(texCoord, g_texelSize, g_blur_sigma, sourceTexture, samplerState);
#endif

#if PASS_BLUR_GAUSSIAN
	// Requirements: Bilinear sampler
	color = Blur_Gaussian(texCoord, sourceTexture, samplerState, g_texelSize, g_blur_direction, g_blur_sigma);
#endif

#if PASS_BLUR_BILATERAL_GAUSSIAN
	// Requirements: Bilinear sampler
	color = Blur_GaussianBilateral(texCoord, sourceTexture, sourceTexture2, sourceTexture3, samplerState, g_texelSize, g_blur_direction, g_blur_sigma);
#endif

#if PASS_BRIGHT
	color 				= sourceTexture.Sample(samplerState, texCoord);
    float luminance 	= Luminance(color.rgb);
	color 				= luminance * color;
#endif

#if PASS_BLEND_ADDITIVE
	float4 sourceColor 	= sourceTexture.Sample(samplerState, texCoord);
	float4 sourceColor2 = sourceTexture2.Sample(samplerState, texCoord);
	color 				= sourceColor + sourceColor2 * g_bloom_intensity;
#endif

#if PASS_LUMA
	color 		= sourceTexture.Sample(samplerState, texCoord);
    color.a 	= Luminance(color.rgb);
#endif

#if PASS_DITHERING
	color 	= sourceTexture.Sample(samplerState, texCoord);
    color 	= Dither_Ordered(color, texCoord);
#endif

#if PASS_TAA_RESOLVE
	color = ResolveTAA(texCoord, sourceTexture, sourceTexture2, sourceTexture3, sourceTexture4, samplerState);
#endif

#if PASS_MOTION_BLUR
	color = MotionBlur(texCoord, sourceTexture, sourceTexture2, samplerState);
#endif

    return color;
}