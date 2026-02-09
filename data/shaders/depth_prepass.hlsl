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

//= INCLUDES ======================
#include "common.hlsl"
#include "common_tessellation.hlsl"
//=================================

#ifdef INDIRECT_DRAW
gbuffer_vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID, [[vk::builtin("DrawIndex")]] uint draw_id : DRAW_INDEX)
{
    _draw = indirect_draw_data_out[draw_id];
#else
gbuffer_vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    _draw = draw_data[buffer_pass.draw_index];
#endif

    float3 position_world          = 0.0f;
    float3 position_world_previous = 0.0f;
    gbuffer_vertex vertex          = transform_to_world_space(input, instance_id, _draw.transform, position_world, position_world_previous);
    vertex.material_index          = _draw.material_index;
    return transform_to_clip_space(vertex, position_world, position_world_previous);
}

void main_ps(gbuffer_vertex vertex)
{
    pass_load_draw_data_from_vertex(vertex.material_index);

    // distance based alpha threshold
    const bool has_albedo       = pass_get_f3_value().y == 1.0f;
    const float2 screen_uv      = vertex.position.xy / buffer_frame.resolution_render;
    const float3 position_world = get_position(vertex.position.z, screen_uv);
    const float alpha_threshold = get_alpha_threshold(position_world);

    if (has_albedo && GET_TEXTURE(material_texture_index_albedo).Sample(samplers[sampler_anisotropic_wrap], vertex.uv_misc.xy).a <= alpha_threshold)
        discard;
}
