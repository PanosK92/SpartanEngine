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

//= INCLUDES =========
#include "Common.hlsl"
//====================

static const float g_chromatic_aberration_intensity = 5.0f;

float4 ChromaticAberration(float2 uv, Texture2D sourceTexture)
{
    float camera_error = 1.0f / g_camera_aperture;
    float intensity = clamp(camera_error * 50.0f, 0.0f, g_chromatic_aberration_intensity);
    float2 shift    = float2(intensity, -intensity);

    // Lens effect
    shift.x *= abs(uv.x * 2.0f - 1.0f);
    shift.y *= abs(uv.y * 2.0f - 1.0f);
    
    // Sample color
	float4 color    = 0.0f; 
    color.r         = sourceTexture.Sample(sampler_bilinear_clamp, uv + (g_texel_size * shift)).r;
    color.ga        = sourceTexture.Sample(sampler_point_clamp, uv).ga;
    color.b         = sourceTexture.Sample(sampler_bilinear_clamp, uv - (g_texel_size * shift)).b;

    return color;
}

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    return ChromaticAberration(input.uv, tex);
}
