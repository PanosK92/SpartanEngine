/*
Copyright(c) 2016-2019 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ======================
#include "Common.hlsl"
#include "Sharpening.hlsl"
#include "ChromaticAberration.hlsl"
#include "Blur.hlsl"
#include "ToneMapping.hlsl"
#include "ResolveTAA.hlsl"
#include "MotionBlur.hlsl"
#include "Dithering.hlsl"
#include "Scaling.hlsl"
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

cbuffer BlurBuffer : register(b1)
{	
	float2 blur_direction;
	float blur_sigma;
	float blur_padding;
}

Pixel_PosUv mainVS(Vertex_PosUv input)
{
    Pixel_PosUv output;
	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, g_viewProjectionOrtho);
    output.uv 			= input.uv;
	
    return output;
}

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    float2 texCoord 	= input.uv;
    float4 color 		= float4(1.0f, 0.0f, 0.0f, 1.0f);

#if PASS_GAMMA_CORRECTION
	color 		= sourceTexture.Sample(samplerState, texCoord);
	color 		= gamma(color);
#endif

#if PASS_TONEMAPPING
	color 		= sourceTexture.Sample(samplerState, texCoord);
	color.rgb 	= ToneMap(color.rgb, g_exposure);
#endif

#if PASS_TEXTURE
	color = sourceTexture.Sample(samplerState, texCoord);
#endif

#if PASS_FXAA
	// Requirements: Bilinear sampler
	FxaaTex tex 				= { samplerState, sourceTexture };
    float2 fxaaQualityRcpFrame	= g_texel_size;
  
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
	color.rgb = ChromaticAberration(texCoord, g_texel_size, sourceTexture, samplerState);
#endif

#if PASS_SHARPENING
	// Requirements: Bilinear sampler
	color.rgb = LumaSharpen(texCoord, sourceTexture, samplerState, g_resolution, g_sharpen_strength, g_sharpen_clamp);	
#endif

#if PASS_UPSAMPLE_BOX
	color = Upsample_Box(texCoord, g_texel_size, sourceTexture, samplerState, 1.0f);
#endif

#if PASS_BLUR_BOX
	color = Blur_Box(texCoord, g_texel_size, blur_sigma, sourceTexture, samplerState);
#endif

#if PASS_BLUR_GAUSSIAN
	// Requirements: Bilinear sampler
	color = Blur_Gaussian(texCoord, sourceTexture, samplerState, g_texel_size, blur_direction, blur_sigma);
#endif

#if PASS_BLUR_BILATERAL_GAUSSIAN
	// Requirements: Bilinear sampler
	color = Blur_GaussianBilateral(texCoord, sourceTexture, sourceTexture2, sourceTexture3, samplerState, g_texel_size, blur_direction, blur_sigma);
#endif

#if PASS_BLOOM_DOWNSAMPLE
	color = Downsample_Box13Tap(texCoord, g_texel_size, sourceTexture, samplerState);
#endif

#if PASS_BLOOM_DOWNSAMPLE_LUMINANCE
	color = Downsample_Box13Tap(texCoord, g_texel_size, sourceTexture, samplerState);
	color = luminance(color) * color;
#endif

#if PASS_BLOOM_BLEND_ADDITIVE
	float4 sourceColor 	= sourceTexture.Sample(samplerState, texCoord);
	float4 sourceColor2 = Upsample_Box(texCoord, g_texel_size, sourceTexture2, samplerState, 2.0f);
	color 				= sourceColor + sourceColor2 * g_bloom_intensity;
#endif

#if PASS_LUMA
	color 	= sourceTexture.Sample(samplerState, texCoord);
    color.a = luminance(color.rgb);
#endif

#if PASS_DITHERING
	color = sourceTexture.Sample(samplerState, texCoord);
    color = Dither_Ordered(color, texCoord);
#endif

#if PASS_TAA_RESOLVE
	color = ResolveTAA(texCoord, sourceTexture, sourceTexture2, sourceTexture3, sourceTexture4, samplerState);
#endif

#if PASS_MOTION_BLUR
	color = MotionBlur(texCoord, sourceTexture, sourceTexture2, sourceTexture3, samplerState);
#endif

#if DEBUG_NORMAL
	float3 normal = sourceTexture.Sample(samplerState, texCoord).rgb;
	normal = pack(normal);
	color = float4(normal, 1.0f);
#endif

#if DEBUG_VELOCITY
	float3 velocity = sourceTexture.Sample(samplerState, texCoord).rgb;
	velocity = abs(velocity) * 20.0f;
	color = float4(velocity, 1.0f);
#endif

#if DEBUG_R_CHANNEL
	float r = sourceTexture.Sample(samplerState, texCoord).r;
	color = float4(r, r, r, 1.0f);
#endif

#if DEBUG_A_CHANNEL
	float a = sourceTexture.Sample(samplerState, texCoord).a;
	color = float4(a, a, a, 1.0f);
#endif

#if DEBUG_RGB_CHANNEL_GAMMA_CORRECT
	float3 rgb 	= sourceTexture.Sample(samplerState, texCoord).rgb;
	rgb 		= gamma(rgb);
	color 		= float4(rgb, 1.0f);
#endif

    return color;
}