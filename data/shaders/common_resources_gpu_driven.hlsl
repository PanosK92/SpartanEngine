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

#ifndef SPARTAN_COMMON_RESOURCES_GPU_DRIVEN
#define SPARTAN_COMMON_RESOURCES_GPU_DRIVEN

#include "shared_buffers.h"

// fft ocean compute targets, texture2d arrays with one slice per cascade
[[vk::image_format("unknown")]] RWTexture2DArray<float4> tex_ocean_spectrum_uav     : register(u9);
[[vk::image_format("unknown")]] RWTexture2DArray<float4> tex_ocean_fft_a_uav        : register(u10);
[[vk::image_format("unknown")]] RWTexture2DArray<float4> tex_ocean_fft_b_uav        : register(u11);
[[vk::image_format("unknown")]] RWTexture2DArray<float4> tex_ocean_displacement_uav : register(u12);
[[vk::image_format("unknown")]] RWTexture2DArray<float4> tex_ocean_normal_uav       : register(u13);

// gpu-driven indirect drawing buffers
// indirect_draw_args is a single-slot buffer used as the args for the final non-indexed indirect draw
//   slot 0 layout matches VkDrawIndirectCommand for the first 16 bytes: vertex_count, instance_count, first_vertex, first_instance
//   the cpu primes instance_count = 1 each frame, vertex_count is atomically bumped by triangle cull (in 3-vertex steps)
#if defined(SP_SHADER_STAGE_COMPUTE)
RWStructuredBuffer<IndirectDrawArgs> indirect_draw_args : register(u31);
#endif
StructuredBuffer<DrawData> indirect_draw_data           : register(t32);
#if defined(SP_SHADER_STAGE_COMPUTE)
RWStructuredBuffer<MeshletInstance> meshlet_instances   : register(u33);
RWStructuredBuffer<uint> visible_triangles              : register(u34);
#else
StructuredBuffer<MeshletInstance> meshlet_instances     : register(t33);
StructuredBuffer<uint> visible_triangles                : register(t34);
#endif
// triangle_dispatch_args drives triangle cull (vs) or mesh draws (mesh), group_count_x is the survivor count
// mesh path uses two slots: [0] opaque, [1] alpha, written by the meshlet cull when split is enabled
RWStructuredBuffer<IndirectDispatchArgs> triangle_dispatch_args : register(u35);

RWStructuredBuffer<Particle>      particle_buffer_a : register(u36);
RWStructuredBuffer<uint>          particle_counter  : register(u38);
RWStructuredBuffer<EmitterParams> particle_emitter  : register(u39);

// gpu texture compression
RWStructuredBuffer<uint>  tex_compress_in      : register(u40);
RWStructuredBuffer<uint4> tex_compress_out     : register(u41); // bc3, bc5 (16 bytes per block)
RWStructuredBuffer<uint2> tex_compress_out_bc1 : register(u42); // bc1 (8 bytes per block)

// the struct is compressed to 24 bytes, see shared_buffers.h MeshletBounds and the meshlet_decode_* helpers below for the dequant
StructuredBuffer<MeshletBounds> meshlet_bounds   : register(t43);

// dequant helpers, kept in one place so the meshlet cull, triangle cull and the visible-triangle vertex pull stay in lockstep
// the lod aabb (min, extent, diag) lives on DrawData per renderable, build_meshlets quantized the bounds against the same aabb on the cpu so this round trip is loss-free for the conservative sphere
float3 meshlet_decode_center(MeshletBounds mb, float3 lod_aabb_min, float3 lod_aabb_extent)
{
    uint cx = mb.center_xy & 0xFFFFu;
    uint cy = (mb.center_xy >> 16) & 0xFFFFu;
    uint cz = mb.center_z_radius & 0xFFFFu;
    float3 t = float3(cx, cy, cz) * (1.0f / 65535.0f);
    return lod_aabb_min + t * lod_aabb_extent;
}

float meshlet_decode_radius(MeshletBounds mb, float lod_aabb_diag)
{
    uint r = (mb.center_z_radius >> 16) & 0xFFFFu;
    return float(r) * (1.0f / 65535.0f) * lod_aabb_diag;
}

uint meshlet_decode_first_index(MeshletBounds mb)
{
    return mb.first_index_tri_count & MESHLET_FIRST_INDEX_MASK;
}

uint meshlet_decode_triangle_count(MeshletBounds mb)
{
    return (mb.first_index_tri_count >> MESHLET_TRI_COUNT_SHIFT) & MESHLET_TRI_COUNT_MASK;
}

uint meshlet_decode_first_vertex(MeshletBounds mb)
{
    return mb.first_vertex_vert_count & MESHLET_FIRST_VERTEX_MASK;
}

uint meshlet_decode_vertex_count(MeshletBounds mb)
{
    return (mb.first_vertex_vert_count >> MESHLET_VERT_COUNT_SHIFT) & MESHLET_VERT_COUNT_MASK;
}

// unique vertex remaps + micro indices for the mesh shader path
StructuredBuffer<uint> meshlet_vertices      : register(t58);
StructuredBuffer<uint> meshlet_micro_indices : register(t59);

// micro indices are packed four corners per uint, a corner is a meshlet local vertex id below MESHLET_MAX_VERTICES so a byte is enough
// the cpu packer pads each mesh block to a multiple of four corners so a block never straddles a uint
uint meshlet_micro_index_load(uint corner)
{
    return (meshlet_micro_indices[corner >> 2u] >> ((corner & 3u) * 8u)) & 0xFFu;
}

// per-instance cull tasks (read-only, declared as rw to keep slot management uniform with other indirect buffers)
RWStructuredBuffer<CullTask> cull_tasks : register(u44);

// gpu-driven two-phase culling, phase a (instance_cull) compacts visible instances into surviving_instances and
// bumps instance_dispatch_args.group_count_x, phase b (indirect_cull) is a DispatchIndirect over that count, one
// workgroup per surviving instance expanding its meshlets
RWStructuredBuffer<SurvivingInstance>    surviving_instances    : register(u37);
RWStructuredBuffer<IndirectDispatchArgs> instance_dispatch_args : register(u55);

// clustered lighting, written by light_cluster_assign and read by light
// grid is (first_index, count) per cluster, indices is the flat list of light slot ids
// single grid shared by both vr eyes, built in the left eye view-projection space, the right eye projects
// its world space samples through the same matrices for the lookup, ipd induced offset is well under one tile
RWStructuredBuffer<uint2> cluster_light_grid    : register(u45);
RWStructuredBuffer<uint>  cluster_light_indices : register(u46);

// cluster assign telemetry, currently a single overflow counter bumped when a cluster overshoots CLUSTER_MAX_LIGHTS
RWStructuredBuffer<uint> cluster_stats : register(u47);

// compact list of volumetric light indices, written by the cpu in UpdateLights, scanned by froxel fog inject
// declared rw for binding uniformity with the other indirect/cluster buffers, treated as read only inside the shader
RWStructuredBuffer<uint> volumetric_light_indices : register(u48);

// fft ocean displacement per texel, one slice per cascade
RWStructuredBuffer<float4> ocean_heights : register(u56);

// restir paired spatial reuse tables, lin 2026 3, packed signed deltas to each pixel's partner
// built once on the cpu, three concatenated tileable tables, treated read-only
RWStructuredBuffer<uint> restir_pairing : register(u57);

// restir nee pool, world space emissive triangles populated by Renderer::BuildEmissiveTriangleNeePool
// each entry packs the world space triangle, area, normal, emission radiance, picking weight and
// a cumulative prefix sum used for area-weighted sampling, count comes via buffer_frame.restir_pt_emissive_tri_count
// declared rw to match the engine pattern for per-pass structured buffers but treated as read-only
RWStructuredBuffer<EmissiveTriangle> emissive_triangles : register(u49);

// gpu procedural grass, written each frame by grass_populate.hlsl and consumed by the grass raster vs
// grass_instances is the transient ring buffer, partitioned into one section per lod via lod_base in the push constant
// grass_count holds one atomic counter per lod, bumped by interlockedadd during the populate dispatch
// grass_indirect_args holds one DrawIndexedIndirect args entry per lod, the args compute reads grass_count
// and bakes index_count / first_index / vertex_offset / first_instance from the per-lod constants
RWStructuredBuffer<GrassInstance>    grass_instances     : register(u50);
RWStructuredBuffer<uint>             grass_count         : register(u51);
RWStructuredBuffer<IndirectDrawArgs> grass_indirect_args : register(u52);
RWStructuredBuffer<uint>             particle_volume_density : register(u53);
RWStructuredBuffer<uint>             particle_volume_color   : register(u54);

#endif
