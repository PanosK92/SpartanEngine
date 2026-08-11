/*
Copyright(c) 2015-2026 Panos Karabelas

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

struct vertex_in
{
    float3 position : POSITION;
    float2 uv       : TEXCOORD;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    nointerpolation float visible : TEXCOORD1;
};

PixelInput main_vs(vertex_in input)
{
    PixelInput output;
    const float2 corner         = input.uv;
    const float2 icon_size      = pass_get_f2_value();
    const float2 resolution     = max(pass_get_f4_value().xy, float2(1.0f, 1.0f));
    float3 world_position       = input.position + buffer_frame.camera_forward.xyz * 0.001f;
    const float3 camera_to_icon = normalize(world_position - buffer_frame.camera_position.xyz);
    output.visible              = dot(buffer_frame.camera_forward.xyz, camera_to_icon) > 0.5f ? 1.0f : 0.0f;
    output.position             = mul(float4(world_position, 1.0f), buffer_frame.view_projection_unjittered);
    output.position.xy         += corner * icon_size / resolution * output.position.w;
    output.uv                   = float2(corner.x * 0.5f + 0.5f, 0.5f - corner.y * 0.5f);
    return output;
}

float4 main_ps(PixelInput input) : SV_TARGET
{
    if (input.visible == 0.0f)
    {
        discard;
    }

    return tex.Sample(samplers[sampler_bilinear_clamp], input.uv);
}
