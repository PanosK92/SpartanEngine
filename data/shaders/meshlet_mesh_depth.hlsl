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

// depth prepass mesh path, opaque emits position only, alpha emits slim uv+material for cutout ps
// one workgroup per survivor, the indirect group count bounds the dispatch

// the opaque and alpha halves are separate shader variants, so the half is a compile time constant
// it used to come from a push constant, which never landed in the mesh stage and filtered out every survivor
#ifdef DEPTH_ALPHA
static const bool want_alpha = true;
#else
static const bool want_alpha = false;
#endif

struct MeshPrimitive
{
    bool cull_primitive : SV_CullPrimitive;
};

groupshared MeshletInstance gs_mi;
groupshared DrawData        gs_draw;
groupshared MeshletBounds   gs_mb;
groupshared uint            gs_vertex_count;
groupshared uint            gs_triangle_count;
groupshared uint            gs_first_vertex;
groupshared uint            gs_first_micro;
groupshared bool            gs_skip_backface;
groupshared bool            gs_emit;
groupshared float3          gs_world[MESHLET_MAX_VERTICES];
groupshared float4          gs_clip[MESHLET_MAX_VERTICES];

[outputtopology("triangle")]
[numthreads(MESH_SHADER_NUMTHREADS, 1, 1)]
void main_ms(
    uint3 group_id : SV_GroupID,
    uint3 thread_id : SV_GroupThreadID,
    out indices uint3 out_indices[MESHLET_MAX_TRIANGLES],
#ifdef DEPTH_ALPHA
    out vertices depth_mesh_vertex out_vertices[MESHLET_MAX_VERTICES],
#else
    out vertices depth_mesh_position out_vertices[MESHLET_MAX_VERTICES],
#endif
    out primitives MeshPrimitive out_primitives[MESHLET_MAX_TRIANGLES]
)
{
    const uint tid = thread_id.x;
    // the cull keeps one contiguous survivor list, both halves walk it and filter on the material flag
    const uint mi_idx = group_id.x;

    if (tid == 0)
    {
        gs_emit           = false;
        gs_vertex_count   = 0u;
        gs_triangle_count = 0u;
        gs_first_vertex   = 0u;
        gs_first_micro    = 0u;
        gs_skip_backface  = false;

        {
            gs_mi   = meshlet_instances[mi_idx];
            gs_draw = indirect_draw_data[gs_mi.draw_index];
            gs_mb   = meshlet_bounds[gs_mi.meshlet_index];

            const bool is_alpha = (gs_draw.flags & 16u) != 0u;
            gs_emit             = (is_alpha == want_alpha);

            if (gs_emit)
            {
                gs_vertex_count   = min(meshlet_decode_vertex_count(gs_mb), MESHLET_MAX_VERTICES);
                gs_triangle_count = min(meshlet_decode_triangle_count(gs_mb), MESHLET_MAX_TRIANGLES);
                gs_first_vertex   = meshlet_decode_first_vertex(gs_mb);
                gs_first_micro    = gs_mb.first_micro;
                gs_skip_backface  = ((gs_draw.flags & 1u) | (gs_draw.flags & 8u)) != 0u;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (!gs_emit)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }

    SetMeshOutputCounts(gs_vertex_count, gs_triangle_count);
    _draw = gs_draw;

    const uint vertex_loops = (MESHLET_MAX_VERTICES + MESH_SHADER_NUMTHREADS - 1) / MESH_SHADER_NUMTHREADS;
    [unroll]
    for (uint v_loop = 0; v_loop < vertex_loops; v_loop++)
    {
        uint v_index = tid + v_loop * MESH_SHADER_NUMTHREADS;
        if (v_index >= gs_vertex_count)
        {
            continue;
        }

        uint local_vertex_id  = meshlet_vertices[gs_first_vertex + v_index];
        uint global_vertex_id = local_vertex_id + gs_draw.lod_vertex_offset;
        Vertex_PosUvNorTan input = pull_vertex(global_vertex_id, gs_mi.instance_index, gs_draw.instance_offset);

        float3 position_world          = 0.0f;
        float3 position_world_previous = 0.0f;
        gbuffer_vertex vertex          = transform_to_world_space(input, gs_mi.instance_index, gs_draw.transform, position_world, position_world_previous);
        vertex.material_index          = gs_draw.material_index;
        gbuffer_vertex clipped         = transform_to_clip_space(vertex, position_world, position_world_previous, 0);

#ifdef DEPTH_ALPHA
        depth_mesh_vertex out_v;
        out_v.position       = clipped.position;
        out_v.uv_misc        = clipped.uv_misc;
        out_v.material_index = clipped.material_index;
        out_v.view_id        = clipped.view_id;
        out_vertices[v_index] = out_v;
#else
        depth_mesh_position out_v;
        out_v.position        = clipped.position;
        out_vertices[v_index] = out_v;
#endif
        gs_world[v_index] = position_world;
        gs_clip[v_index]  = clipped.position;
    }

    GroupMemoryBarrierWithGroupSync();

    const uint triangle_loops = (MESHLET_MAX_TRIANGLES + MESH_SHADER_NUMTHREADS - 1) / MESH_SHADER_NUMTHREADS;
    [unroll]
    for (uint t_loop = 0; t_loop < triangle_loops; t_loop++)
    {
        uint t_index = tid + t_loop * MESH_SHADER_NUMTHREADS;
        if (t_index >= gs_triangle_count)
        {
            continue;
        }

        uint corner = gs_first_micro + t_index * 3u;
        uint i0     = meshlet_micro_index_load(corner + 0u);
        uint i1     = meshlet_micro_index_load(corner + 1u);
        uint i2     = meshlet_micro_index_load(corner + 2u);

        bool cull = false;

        if (!gs_skip_backface)
        {
            float3 p0w = gs_world[i0];
            float3 p1w = gs_world[i1];
            float3 p2w = gs_world[i2];
            float3 face_normal = cross(p1w - p0w, p2w - p0w);
            float3 view_dir    = p0w - buffer_frame.camera_position;
            cull = dot(face_normal, view_dir) > 0.0f;
        }

        float4 p0 = gs_clip[i0];
        float4 p1 = gs_clip[i1];
        float4 p2 = gs_clip[i2];

        if (!cull && p0.w > 0.0f && p1.w > 0.0f && p2.w > 0.0f)
        {
            float2 n0 = p0.xy / p0.w;
            float2 n1 = p1.xy / p1.w;
            float2 n2 = p2.xy / p2.w;
            float2 ndc_min = min(n0, min(n1, n2));
            float2 ndc_max = max(n0, max(n1, n2));
            float2 extent  = (ndc_max - ndc_min) * get_render_resolution_active() * 0.5f;
            cull = max(extent.x, extent.y) < 0.5f;
        }

        MeshPrimitive prim;
        prim.cull_primitive     = cull;
        out_primitives[t_index] = prim;
        out_indices[t_index]    = uint3(i0, i1, i2);
    }
}
