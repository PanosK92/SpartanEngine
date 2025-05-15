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
    uint index_light  = (uint)f3_value_2.x;
    uint index_array  = (uint)f3_value_2.y;
    Light light;
    Surface surface;
    light.Build(index_light, surface);

    gbuffer_vertex vertex         = transform_to_world_space(input, instance_id, buffer_pass.transform);
    vertex.position_clip          = mul(float4(vertex.position, 1.0f), light.transform[index_array]);
    vertex.position_clip_current  = 0.0f; // this and the previous position to be given a value other wise you get validation errors
    vertex.position_clip_previous = 0.0f; // ideally, we just call transform_to_clip_space() like the gbuffer but it works for the light as well
    
    // for point lights, output.position is in view space this because we do the paraboloid projection here
    if (light.is_point())
    {
        float3 ndc           = project_onto_paraboloid(vertex.position_clip.xyz, light.near, light.far);
        vertex.position_clip = float4(ndc, 1.0f);
    }

    return vertex;
}

void main_ps(gbuffer_vertex vertex)
{
    const bool has_albedo = pass_get_f3_value().x == 1.0f;
    float alpha_threshold = get_alpha_threshold(vertex.position); // distance based alpha threshold

    if (has_albedo && GET_TEXTURE(material_texture_index_albedo).Sample(samplers[sampler_anisotropic_wrap], vertex.uv).a <= alpha_threshold)
        discard;
}
