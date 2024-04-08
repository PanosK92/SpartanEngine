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

#define TRANSFORM_LIGHT

//= INCLUDES =========
#include "common.hlsl"
//====================

Pixel_PosUv main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    Pixel_PosUv output;
    output.uv = input.uv;

    uint index_light = (uint)pass_get_f3_value2().y;
    uint index_array = (uint)pass_get_f3_value2().x;
    Light_ light     = buffer_lights[index_light];

    float3 position_world = transform_to_world_space(input, instance_id, buffer_pass.transform);
    output.position = mul(float4(position_world, 1.0f), light.view_projection[index_array]);
    
    return output;
}

float4 main_ps(Pixel_PosUv input) : SV_TARGET
{
    // alpha test
    const float3 f3_value     = pass_get_f3_value();
    const bool has_alpha_mask = f3_value.x == 1.0f;
    const bool has_albedo     = f3_value.y == 1.0f;
    float alpha_mask          = has_alpha_mask ? GET_TEXTURE(material_mask).Sample(samplers[sampler_point_wrap], input.uv).r : 1.0f;
    bool alpha_albedo         = has_albedo     ? GET_TEXTURE(material_albedo).Sample(samplers[sampler_point_wrap], input.uv).a : 1.0f;
    if (min(alpha_mask, alpha_albedo) <= ALPHA_THRESHOLD_DEFAULT)
        discard;

    // colored transparent shadows
    return GetMaterial().color;
}
