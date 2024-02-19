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
#include "common.hlsl"
//====================

struct PixelIn
{
    float4 position       : SV_POSITION;
    float2 uv             : TEXCOORD;
    float3 world_position : TEXCOORD1;
};

PixelIn mainVS(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    PixelIn output;
    
    output.position = compute_screen_space_position(input, instance_id, buffer_pass.transform, buffer_frame.view_projection, buffer_frame.time, output.world_position);
    output.uv       = input.uv;
    
    return output;
}

// alpha test
void mainPS(PixelIn input)
{
    const float3 f3_value     = pass_get_f3_value();
    const bool has_alpha_mask = f3_value.x == 1.0f;
    const bool has_albedo     = f3_value.y == 1.0f;
    const float alpha         = f3_value.z;

    float alpha_threshold = get_alpha_threshold(input.world_position);
    bool mask_alpha       = has_alpha_mask && GET_TEXTURE(material_mask).Sample(samplers[sampler_point_wrap], input.uv).r <= alpha_threshold;
    bool mask_albedo      = alpha == 1.0f && has_albedo && GET_TEXTURE(material_albedo).Sample(samplers[sampler_anisotropic_wrap], input.uv).a <= alpha_threshold;
    
    if (mask_alpha || mask_albedo)
        discard;
}
