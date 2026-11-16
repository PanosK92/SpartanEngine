/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_GRASS_MESH_LAYOUT
#define SPARTAN_GRASS_MESH_LAYOUT

// mesh shader grass, shared by grass_indirect_args.hlsl, which sizes the mesh draws, and grass_mesh.hlsl, which runs them
//
// a section is one draw list of grass_instances, the fine blades of a ring or the coarse blades of a ring, laid out like
// grass_indirect_args, entry [ring] fine and entry [ring + grass_ring_count] coarse

// per section, first_index, index_count, vertex_offset and vertex_count of its blade lod in the geometry buffer, written
// by the cpu when a slot's mesh is baked
StructuredBuffer<uint4> grass_mesh_geometry : register(t64);

// per section, the mesh draw's group counts and, in w, the blades it builds
#if defined(SP_SHADER_STAGE_COMPUTE)
RWStructuredBuffer<uint4> grass_mesh_args : register(u58);
#else
StructuredBuffer<uint4> grass_mesh_args : register(t67);
#endif

// a mesh group holds 256 vertices and 256 triangles
static const uint grass_mesh_max_vertices  = 256;
static const uint grass_mesh_max_triangles = 256;

// as many blades as fit, 19 near blades (13 vertices, 11 triangles), 36 mid ones (7, 5) or 64 far ones (4, 2)
uint grass_mesh_blades_per_group(uint4 geometry)
{
    uint blade_vertices  = max(geometry.w, 1u);
    uint blade_triangles = max(geometry.y / 3u, 1u);
    return max(1u, min(grass_mesh_max_vertices / blade_vertices, grass_mesh_max_triangles / blade_triangles));
}

#endif // SPARTAN_GRASS_MESH_LAYOUT
