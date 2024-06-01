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

#define TRANSFORM_IGNORE_NORMALS
#define TRANSFORM_IGNORE_PREVIOUS_POSITION

//= INCLUDES =========
#include "common.hlsl"
//====================

struct vertex
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    vertex output;
    output.uv = input.uv;

    float3 f3_value_2 = pass_get_f3_value2();
    uint index_array  = (uint)f3_value_2.y;

    Light light;
    light.Build();

    gbuffer_vertex vertex = transform_to_world_space(input, instance_id, buffer_pass.transform);
    output.position       = mul(float4(vertex.position, 1.0f), light.transform[index_array]);

    // for point lights, output.position is in view space this because we do the paraboloid projection here
    if (light.is_point())
    {
        float3 ndc      = project_onto_paraboloid(output.position.xyz, light.near, light.far);
        output.position = float4(ndc, 1.0);
    }

    return output;
}

 float4 main_ps(vertex input) : SV_Target0
{
    // alpha test
    const float3 f3_value     = pass_get_f3_value();
    const bool has_alpha_mask = f3_value.x == 1.0f;
    const bool has_albedo     = f3_value.y == 1.0f;
    float alpha_mask          = has_alpha_mask ? GET_TEXTURE(material_mask).Sample(samplers[sampler_point_wrap], input.uv).r : 1.0f;
    bool alpha_albedo         = has_albedo ? GET_TEXTURE(material_albedo).Sample(samplers[sampler_point_wrap], input.uv).a : 1.0f;
    if (min(alpha_mask, alpha_albedo) <= ALPHA_THRESHOLD_DEFAULT)
        discard;

    // colored transparent shadows
    return GetMaterial().color;
}
