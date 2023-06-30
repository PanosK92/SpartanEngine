/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "common.hlsl"
//====================

// texture visualization options
bool texture_pack()             { return buffer_imgui.texture_flags & uint(1U << 0); }
bool texture_gamma_correction() { return buffer_imgui.texture_flags & uint(1U << 1); }
bool texture_boost()            { return buffer_imgui.texture_flags & uint(1U << 2); }
bool texture_abs()              { return buffer_imgui.texture_flags & uint(1U << 3); }
bool texture_channel_r()        { return buffer_imgui.texture_flags & uint(1U << 4); }
bool texture_channel_g()        { return buffer_imgui.texture_flags & uint(1U << 5); }
bool texture_channel_b()        { return buffer_imgui.texture_flags & uint(1U << 6); }
bool texture_channel_a()        { return buffer_imgui.texture_flags & uint(1U << 7); }
bool texture_sample_point()     { return buffer_imgui.texture_flags & uint(1U << 8); }

Pixel_PosColUv mainVS(Vertex_Pos2dUvColor input)
{
    Pixel_PosColUv output;

    output.position = mul(buffer_imgui.transform, float4(input.position.x, input.position.y, 0.0f, 1.0f));
    output.color    = input.color;
    output.uv       = input.uv;

    return output;
}

float4 mainPS(Pixel_PosColUv input) : SV_Target
{
    float mip            = (float)buffer_imgui.mip_level;
    float4 color_texture = texture_sample_point() ? tex.SampleLevel(sampler_point_wrap, input.uv, mip) : tex.SampleLevel(sampler_bilinear_wrap, input.uv, mip);
    float4 color_vertex  = input.color;

    // Set requested channels channels
    if (buffer_imgui.texture_flags != 0)
    {
        color_texture.r *= texture_channel_r() ? 1.0f : 0.0f;
        color_texture.g *= texture_channel_g() ? 1.0f : 0.0f;
        color_texture.b *= texture_channel_b() ? 1.0f : 0.0f;
        color_texture.a  = texture_channel_a() ? color_texture.a : 1.0f;
    }

    if (texture_gamma_correction())
    {
        color_texture.rgb = gamma(color_texture.rgb);
    }

    if (texture_abs())
    {
        color_texture = abs(color_texture);
    }

    if (texture_pack())
    {
        color_texture.rgb = pack(color_texture.rgb);
    }

    if (texture_boost())
    {
        color_texture.rgb *= 10.0f;
    } 

    return color_vertex * color_texture;
}
