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
#include "common.hlsl"
//====================

Pixel_PosNor mainVS(Vertex_PosUvNorTan input)
{
    Pixel_PosNor output;

    float4x4 wvp = mul(buffer_pass.transform, buffer_frame.view_projection_unjittered);

    input.position.w = 1.0f;
    output.position  = mul(input.position, wvp);
    output.normal    = normalize(mul(input.normal, (float3x3)buffer_pass.transform)).xyz;

    return output;
}

float4 mainPS(Pixel_PosNor input) : SV_TARGET
{
    return float4(tex_reflection_probe.SampleLevel(samplers[sampler_bilinear_clamp], reflect(buffer_frame.camera_direction, input.normal), 0.0f).rgb, 1.0f);
}
