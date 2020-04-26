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

float3 ChromaticAberration(float2 uv, Texture2D sourceTexture)
{
    float2 shift    = float2(2.5f, -2.5f);  //  [-10, 10]
    float strength  = 0.75f;                //  [0, 1]
    
    // supposedly, lens effect
    shift.x *= abs(uv.x * 2.0f - 1.0f);
    shift.y *= abs(uv.y * 2.0f - 1.0f);
    
    float3 color        = float3(0.0f, 0.0f, 0.0f);
    float3 colorInput   = sourceTexture.Sample(sampler_point_clamp, uv).rgb;
    
    // sample the color components
    color.r = sourceTexture.Sample(sampler_bilinear_clamp, uv + (g_texel_size * shift)).r;
    color.g = colorInput.g;
    color.b = sourceTexture.Sample(sampler_bilinear_clamp, uv - (g_texel_size * shift)).b;

    // adjust the strength of the effect
    return lerp(colorInput, color, strength);
}