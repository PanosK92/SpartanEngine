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
gbuffer_vertex main_vs(uint vertex_id : SV_VertexID, uint view_id : SV_ViewID)
{
    MeshletInstance mi;
    Vertex_PosUvNorTan input = pull_visible_triangle_vertex(vertex_id, mi);
    uint instance_id         = mi.instance_index;
#elif defined(GRASS_INSTANCED)
gbuffer_vertex main_vs(Vertex_PosUvNorTan_Cpu cpu_input, uint instance_id : SV_InstanceID, uint view_id : SV_ViewID)
{
    Vertex_PosUvNorTan input = to_full_vertex(cpu_input);
    // pull the per-instance transform from the dedicated procedural grass buffer
    uint slot        = instance_id + (uint)buffer_pass.values[0].z;
    GrassInstance gi = grass_instances[slot];
    input.instance_position_x = gi.pos_x;
    input.instance_position_y = gi.pos_y;
    input.instance_position_z = gi.pos_z;
    input.instance_normal_oct = (gi.normal_yaw_scale >> 16) & 0xFFFFu;
    input.instance_yaw        = (gi.normal_yaw_scale >> 8)  & 0xFFu;
    input.instance_scale      =  gi.normal_yaw_scale        & 0xFFu;
    _draw                    = (DrawData)0;
    _draw.transform          = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    _draw.transform_previous = _draw.transform;
    _draw.material_index     = buffer_pass.material_index;
    _draw.uv_tiling          = float2(1.0f, 1.0f);
#elif defined(INDEXED_MULTI_DRAW)
gbuffer_vertex main_vs(Vertex_PosUvNorTan_Cpu cpu_input, uint instance_or_draw_index : SV_InstanceID, uint view_id : SV_ViewID)
{
    Vertex_PosUvNorTan input = to_full_vertex(cpu_input);
    const bool is_multi_draw = buffer_pass.draw_index == 0xffffffffu;
    _draw                    = draw_data[is_multi_draw ? instance_or_draw_index : buffer_pass.draw_index];
    uint instance_id         = is_multi_draw ? _draw.instance_index : instance_or_draw_index;
#else
gbuffer_vertex main_vs(Vertex_PosUvNorTan_Cpu cpu_input, uint instance_id : SV_InstanceID, uint view_id : SV_ViewID)
{
    Vertex_PosUvNorTan input = to_full_vertex(cpu_input);
    _draw = draw_data[buffer_pass.draw_index];
#endif

    float3 position_world          = 0.0f;
    float3 position_world_previous = 0.0f;
    gbuffer_vertex vertex          = transform_to_world_space(input, instance_id, _draw.transform, position_world, position_world_previous);
    vertex.material_index          = _draw.material_index;
    return transform_to_clip_space(vertex, position_world, position_world_previous, view_id);
}

#ifdef ALPHA_TEST_INDIRECT
// indirect path discards based on material flags read from the bindless material parameters
// non-alpha-tested materials early out so they only pay vertex cost in the prepass
void main_ps(gbuffer_vertex vertex)
{
    pass_load_draw_data_from_vertex(vertex.material_index);

    MaterialParameters material = GetMaterial();
    if (!material.is_alpha_tested())
        return;

    const float2 screen_uv      = vertex.position.xy / get_render_resolution_active();
    const float3 position_world = get_position_for_view(vertex.position.z, screen_uv, vertex.view_id);
    const float alpha_threshold = get_alpha_threshold(position_world);

    float a = GET_TEXTURE(material_texture_index_albedo).Sample(samplers[sampler_anisotropic_wrap], vertex.uv_misc.xy).a;
    if (a <= alpha_threshold)
        discard;
}
#else
void main_ps(gbuffer_vertex vertex)
{
    pass_load_draw_data_from_vertex(vertex.material_index);

    // distance based alpha threshold
    // in multiview the depth prepass is drawn once for both eyes, so buffer_pass.eye_index is
    // static and cannot be used to pick the right eye's inverse vp; drive the per-fragment
    // eye from the interpolated SV_ViewID (vertex.view_id) instead.
    const bool has_albedo       = pass_get_f3_value().y == 1.0f;
    const float2 screen_uv      = vertex.position.xy / get_render_resolution_active();
    const float3 position_world = get_position_for_view(vertex.position.z, screen_uv, vertex.view_id);
    const float alpha_threshold = get_alpha_threshold(position_world);

    if (has_albedo && GET_TEXTURE(material_texture_index_albedo).Sample(samplers[sampler_anisotropic_wrap], vertex.uv_misc.xy).a <= alpha_threshold)
        discard;
}
#endif
