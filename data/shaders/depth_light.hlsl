/*
Copyright(c) 2015-2025 Panos Karabelas

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

gbuffer_vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    float3 f3_value_2 = pass_get_f3_value2();
    uint index_light  = (uint)f3_value_2.x; // index of the light into the bindless array
    uint index_array  = (uint)f3_value_2.y; // index of a particular slice of the texture array of the light
    
    Light light;
    Surface surface;
    light.Build(index_light, surface);

    float3 position_world          = 0.0f;
    float3 position_world_previous = 0.0f;
    gbuffer_vertex vertex          = transform_to_world_space(input, instance_id, buffer_pass.transform, position_world, position_world_previous);
    vertex.position                = mul(float4(position_world, 1.0f), light.transform[index_array]);

    return vertex;
}

void main_ps(gbuffer_vertex vertex)
{
    // distance based alpha threshold
    const bool has_albedo       = pass_get_f3_value().x == 1.0f;
    const float2 screen_uv      = vertex.position.xy / buffer_frame.resolution_render;
    const float3 position_world = get_position(vertex.position.z, screen_uv);
    float alpha_threshold       = get_alpha_threshold(position_world);
    
    if (has_albedo && GET_TEXTURE(material_texture_index_albedo).Sample(samplers[sampler_anisotropic_wrap], vertex.uv_misc.xy).a <= alpha_threshold)
        discard;
}
