/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "output.hlsl"
//====================

struct Vertex_Pos2dUvColor
{
    float2 position : POSITION0;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct vertex
{
    float4 position : SV_POSITION;
    float4 color    : COLOR0;
    float2 uv       : TEXCOORD;
};

vertex main_vs(Vertex_Pos2dUvColor input)
{
    vertex output;

    output.position = mul(buffer_pass.transform, float4(input.position.x, input.position.y, 0.0f, 1.0f));
    output.color    = input.color;
    output.uv       = input.uv;

    return output;
}

float4 main_ps(vertex input) : SV_Target
{
     // texture visualization options
    float4 channels       = pass_get_f4_value();
    float3 f3_value       = pass_get_f3_value();
    bool gamma_correct    = f3_value.x == 1.0f;
    bool packed           = f3_value.y == 1.0f;
    bool boost            = f3_value.z == 1.0f;
    float3 f3_value2      = pass_get_f3_value2();
    bool absolute         = f3_value2.x == 1.0f;
    bool point_sampling   = f3_value2.y == 1.0f;
    float mip             = f3_value2.z;
    bool is_visualized    = pass_is_transparent();
    uint is_frame_texture = pass_get_material_index();

    float4 color_texture = point_sampling ? tex.SampleLevel(samplers[sampler_point_wrap], input.uv, mip) : tex.SampleLevel(samplers[sampler_bilinear_wrap], input.uv, mip);
 
    if (is_visualized)
    {
        color_texture.r *= channels.r ? 1.0f : 0.0f;
        color_texture.g *= channels.g ? 1.0f : 0.0f;
        color_texture.b *= channels.b ? 1.0f : 0.0f;
        color_texture.a  = channels.a ? color_texture.a : 1.0f;
    
        if (gamma_correct)
        {
            color_texture.rgb = linear_to_srgb(color_texture.rgb);
        }
    
        if (absolute)
        {
            color_texture = abs(color_texture);
        }
    
        if (packed)
        {
            color_texture.rgb = pack(color_texture.rgb);
        }
    
        if (boost)
        {
            color_texture.rgb *= 10.0f;
        }
    }

    float4 color = input.color * color_texture;

    if (buffer_frame.hdr_enabled != 0.0f && is_frame_texture == 0)
    {
        color.rgb = srgb_to_linear(color.rgb);
        float white_point = 400.0f;
        color.rgb = linear_to_hdr10(color.rgb, white_point);
    }
    
    return color;
 }
