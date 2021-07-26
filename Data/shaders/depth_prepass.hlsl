/*
Copyright(c) 2016-2021 Panos Karabelas

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

Pixel_PosUv mainVS(Vertex_PosUv input)
{
    Pixel_PosUv output;

    // position computation has to be an exact match to gbuffer.hlsl
    input.position.w    = 1.0f; 
    output.position     = mul(input.position, g_transform);
    output.position     = mul(output.position, g_view_projection);

    output.uv = input.uv;

    return output;
}

void mainPS(Pixel_PosUv input)
{
    if (g_is_transparent_pass && tex_material_mask.Sample(sampler_anisotropic_wrap, input.uv).r <= alpha_mask_threshold)
        discard;

    if (g_color.a == 1.0f && tex_material_albedo.Sample(sampler_anisotropic_wrap, input.uv).a <= alpha_mask_threshold)
        discard;
}
