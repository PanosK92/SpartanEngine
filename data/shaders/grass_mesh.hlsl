/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// grass blades built by a mesh shader from the instances grass_populate.hlsl placed, it replaces the instanced vertex
// draw on devices with mesh shaders, every blade vertex is shaded exactly once, where the instanced draw shades a
// vertex again whenever it falls out of the post transform cache
//
// compiled with GRASS_INSTANCED and GRASS_SPECIALIZED, the same defines as the instanced blade vertex shader, so every
// vertex goes through the same transform, wind, tire and body interaction, and the pixel shader is the one that path uses
//
// the bend a blade shares between its vertices, the wind, the resting arch and the slope limit, is computed once per
// blade and frame instead of once per vertex, see grass_bend_compute in common_vertex_processing.hlsl

//= INCLUDES ===================
#define GRASS_BEND_PRECOMPUTED
#include "g_buffer.hlsl"
#include "grass_mesh_layout.hlsl"
//==============================

static const uint grass_mesh_threads = 32;

groupshared float4x4   blade_transforms[grass_mesh_max_blades];
groupshared grass_bend blade_bends_current[grass_mesh_max_blades];
groupshared grass_bend blade_bends_previous[grass_mesh_max_blades];
groupshared float2 blade_interactions[2][grass_mesh_max_blades];
groupshared float4 blade_roots[2][grass_mesh_max_blades];

[outputtopology("triangle")]
[numthreads(grass_mesh_threads, 1, 1)]
void main_ms(
    uint  thread_index : SV_GroupThreadID,
    uint3 group_id     : SV_GroupID,
    out vertices gbuffer_scatter_vertex vertices[GRASS_MESH_MAX_VERTICES],
    out indices uint3 triangles[GRASS_MESH_MAX_TRIANGLES]
)
{
    // draw_index is the section, the grid is 2d because metal drops more than 65535 groups in one dimension
    uint  section          = buffer_pass.draw_index;
    uint4 geometry         = grass_mesh_geometry[section];
    uint  section_blades   = grass_mesh_args[section].w;
    uint  blades_per_group = grass_mesh_blades_per_group(geometry);
    uint  group_index      = group_id.y * MESH_GRID_WIDTH + group_id.x;
    uint  first_blade      = group_index * blades_per_group;
    uint  blade_count      = first_blade < section_blades ? min(blades_per_group, section_blades - first_blade) : 0u;
    uint  blade_vertices   = geometry.w;
    uint  blade_triangles  = geometry.y / 3u;

    SetMeshOutputCounts(blade_count * blade_vertices, blade_count * blade_triangles);

    // identity draw data, the instance carries the world transform, the same as the instanced vertex shader
    _draw                    = (DrawData)0;
    _draw.transform          = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    _draw.transform_previous = _draw.transform;
    _draw.material_index     = buffer_pass.material_index;
    _draw.uv_tiling          = float2(1.0f, 1.0f);

    uint lod_base      = pass_uint(pass_grass_draw::lod_base);

    // per blade, the instance transform and the bend of this frame and the previous one, for the motion vectors
    for (uint blade = thread_index; blade < blade_count; blade += grass_mesh_threads)
    {
        uint          instance_id = first_blade + blade;
        uint          slot        = lod_base + instance_id;
        GrassInstance gi          = grass_instances[slot];
        float4x4      transform   = compose_instance_transform(gi.pos_x, gi.pos_y, gi.pos_z,
            (gi.normal_yaw_scale >> 16) & 0xFFFFu, (gi.normal_yaw_scale >> 8) & 0xFFu, gi.normal_yaw_scale & 0xFFu);

        float3 root = transform[3].xyz;
        float3 up   = normalize(transform[1].xyz);
        Surface surface;
        surface.flags = 1u << 11; // grass blade
        [unroll]
        for (uint frame = 0; frame < 2; ++frame)
        {
            float time_offset = frame == 0 ? 0.0f : -buffer_frame.delta_time;
            float3 pressure = grass_root_pressure(surface, root, time_offset);
            blade_interactions[frame][blade] = sample_vegetation_interaction(root.xz, time_offset);
            blade_roots[frame][blade] = float4(pressure, grass_camera_bias(root, up, transform[2].xyz, pressure.z, time_offset));
        }
        blade_transforms[blade]     = transform;
        blade_bends_current[blade]  = grass_bend_compute(true, root, up, 0.0f, instance_id);
        blade_bends_previous[blade] = grass_bend_compute(true, root, up, -buffer_frame.delta_time, instance_id);
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint vertex_index = thread_index; vertex_index < blade_count * blade_vertices; vertex_index += grass_mesh_threads)
    {
        uint blade        = vertex_index / blade_vertices;
        uint blade_vertex = vertex_index - blade * blade_vertices;
        uint instance_id  = first_blade + blade;

        PulledVertex pulled = load_pulled_vertex(geometry.z + blade_vertex);
        Vertex_PosUvNorTan input;
        input.position           = pulled.position;
        input.vertex_id          = blade_vertex;
        input.uv_packed          = pulled.uv;
        input.normal_packed      = pulled.normal;
        input.tangent_packed     = pulled.tangent;
        input.instance_transform = blade_transforms[blade];

        grass_interaction_precomputed[0] = blade_interactions[0][blade];
        grass_interaction_precomputed[1] = blade_interactions[1][blade];
        grass_root_precomputed[0] = blade_roots[0][blade];
        grass_root_precomputed[1] = blade_roots[1][blade];
        grass_bend_precomputed[0] = blade_bends_current[blade];
        grass_bend_precomputed[1] = blade_bends_previous[blade];

        float3 position_world             = 0.0f;
        float3 position_world_previous    = 0.0f;
        float3 position_relative          = 0.0f;
        float3 position_relative_previous = 0.0f;
        gbuffer_vertex vertex             = transform_to_world_space(input, instance_id, _draw.transform, position_world, position_world_previous, position_relative, position_relative_previous);
        vertex.material_index             = _draw.material_index;
        vertex                            = transform_to_clip_space(vertex, position_relative, position_relative_previous, 0);
        vertices[vertex_index]            = pack_gbuffer_scatter(vertex);
    }

    for (uint triangle_index = thread_index; triangle_index < blade_count * blade_triangles; triangle_index += grass_mesh_threads)
    {
        uint blade                = triangle_index / blade_triangles;
        uint blade_triangle       = triangle_index - blade * blade_triangles;
        uint first_index          = geometry.x + blade_triangle * 3u;
        uint vertex_base          = blade * blade_vertices;
        triangles[triangle_index] = uint3(
            geometry_indices[first_index + 0u],
            geometry_indices[first_index + 1u],
            geometry_indices[first_index + 2u]
        ) + vertex_base;
    }
}
