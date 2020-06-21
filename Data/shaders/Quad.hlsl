/*
Copyright(c) 2016-2020 Panos Karabelas

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

Pixel_PosUv mainVS(Vertex_PosUv input)
{
    Pixel_PosUv output;
    
    input.position.w    = 1.0f;
    output.position     = mul(input.position, g_viewProjectionOrtho);
    output.uv           = input.uv;
    
    return output;
}

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    float2 uv       = input.uv;
    float4 color    = float4(1.0f, 0.0f, 0.0f, 1.0f);

#if PASS_GAMMA_CORRECTION
    color       = tex.Sample(sampler_point_clamp, uv);
    color       = gamma(color);
#endif

#if PASS_TONEMAPPING
    color       = tex.Sample(sampler_point_clamp, uv);
    color.rgb   = ToneMap(color.rgb);
#endif

#if PASS_TEXTURE
    color = tex.Sample(sampler_bilinear_clamp, uv);
#endif

#if PASS_FXAA
    FxaaTex fxaa_tex            = { sampler_bilinear_clamp, tex };
    float2 fxaaQualityRcpFrame  = g_texel_size;

    float fxaa_subPix           = 0.75f;
    float fxaa_edgeThreshold    = 0.166f;
    float fxaa_edgeThresholdMin = 0.0833f;
    
    color.rgb = FxaaPixelShader
    ( 
        uv, 0, fxaa_tex, fxaa_tex, fxaa_tex,
        fxaaQualityRcpFrame, 0, 0, 0,
        fxaa_subPix,
        fxaa_edgeThreshold,
        fxaa_edgeThresholdMin,
        0, 0, 0, 0
    ).rgb;
    color.a = 1.0f;
#endif

#if PASS_CHROMATIC_ABERRATION
    color.rgb = ChromaticAberration(uv, tex);
#endif

#if PASS_LUMA_SHARPEN
    color.rgb = LumaSharpen(uv, tex, g_resolution, g_sharpen_strength, g_sharpen_clamp);    
#endif

#if PASS_TAA_RESOLVE
    color = ResolveTAA(uv, tex, tex2);
#endif

#if PASS_TAA_SHARPEN
    color = SharpenTaa(uv, tex);    
#endif

#if PASS_UPSAMPLE_BOX
    color = Upsample_Box(uv, tex);
#endif

#if PASS_DOWNSAMPLE_BOX
    color = Downsample_Box(uv, tex);
#endif

#if PASS_BLUR_BOX
    color = Blur_Box(uv, tex);
#endif

#if PASS_BLUR_GAUSSIAN
    color = Blur_Gaussian(uv, tex);
#endif

#if PASS_BLUR_BILATERAL_GAUSSIAN
    color = Blur_GaussianBilateral(uv, tex);
#endif

#if PASS_BLOOM_DOWNSAMPLE
    color = Downsample_BoxAntiFlicker(uv, tex);
#endif

#if PASS_BLOOM_DOWNSAMPLE_LUMINANCE
    color = Downsample_BoxAntiFlicker(uv, tex);
    color = saturate_16(luminance(color) * color);
#endif

#if PASS_BLOOM_BLEND_ADDITIVE
    float4 sourceColor  = tex.Sample(sampler_point_clamp, uv);
    float4 sourceColor2 = Upsample_Box(uv, tex2);
    color               = saturate_16(sourceColor + sourceColor2 * g_bloom_intensity);
#endif

#if PASS_LUMA
    color   = tex.Sample(sampler_point_clamp, uv);
    color.a = luminance(color.rgb);
#endif

#if PASS_DITHERING
    color = tex.Sample(sampler_point_clamp, uv);
    color.rgb += dither(uv);
#endif

#if PASS_MOTION_BLUR
    color = MotionBlur(uv, tex);
#endif

#if DEBUG_NORMAL
    float3 normal = tex.Sample(sampler_point_clamp, uv).rgb;
    normal = pack(normal);
    color = float4(normal, 1.0f);
#endif

#if DEBUG_VELOCITY
    float3 velocity = tex.Sample(sampler_point_clamp, uv).rgb;
    velocity = abs(velocity) * 20.0f;
    color = float4(velocity, 1.0f);
#endif

#if DEBUG_R_CHANNEL
    float r = tex.Sample(sampler_point_clamp, uv).r;
    color = float4(r, r, r, 1.0f);
#endif

#if DEBUG_A_CHANNEL
    float a = tex.Sample(sampler_point_clamp, uv).a;
    color = float4(a, a, a, 1.0f);
#endif

#if DEBUG_RGB_CHANNEL_GAMMA_CORRECT
    float3 rgb  = tex.Sample(sampler_point_clamp, uv).rgb;
    rgb         = gamma(rgb);
    color       = float4(rgb, 1.0f);
#endif

    return color;
}
