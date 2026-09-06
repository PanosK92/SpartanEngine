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
#include "common_vertex_processing.hlsl"
//=================================

// debug pass that draws every post-cull indirect entry to debug_output as a flat hashed color
// uses depth read-equal so only the visible fragment of each meshlet is colored

struct vis_vertex
{
    float4 position                    : SV_POSITION;
    nointerpolation uint meshlet_index : TEXCOORD0;
    nointerpolation uint draw_id       : TEXCOORD1;
};

// integer hash producing three pseudo-random bytes packed into the low 24 bits
uint hash_u32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float3 meshlet_color(uint id)
{
    uint h  = hash_u32(id + 0x9e3779b9u);
    float r = ((h      ) & 0xffu) / 255.0f;
    float g = ((h >> 8 ) & 0xffu) / 255.0f;
    float b = ((h >> 16) & 0xffu) / 255.0f;
    // lift saturation a bit so adjacent meshlets read clearly
    float3 c = float3(r, g, b);
    return lerp(0.15f, 1.0f, c);
}

vis_vertex main_vs(uint vertex_id : SV_VertexID, uint view_id : SV_ViewID)
{
    MeshletInstance mi;
    Vertex_PosUvNorTan input = pull_visible_triangle_vertex(vertex_id, mi);
    uint instance_id         = mi.instance_index;

    float3 position_world          = 0.0f;
    float3 position_world_previous = 0.0f;
    gbuffer_vertex base            = transform_to_world_space(input, instance_id, _draw.transform, position_world, position_world_previous);
    base                           = transform_to_clip_space(base, position_world, position_world_previous, view_id);

    vis_vertex o;
    o.position      = base.position;
    o.meshlet_index = mi.meshlet_index;
    // post-cull triangle id, monotonic across the visible-triangle list
    o.draw_id       = vertex_id / 3u;
    return o;
}

float4 main_ps(vis_vertex v) : SV_Target0
{
    // f3_value.x: 0 = color by global meshlet index, 1 = color by post-cull draw id
    uint mode = (uint)pass_get_f3_value().x;
    uint id   = (mode == 0u) ? v.meshlet_index : v.draw_id;
    return float4(meshlet_color(id), 1.0f);
}
