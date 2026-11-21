/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// batched impostor mesh shader: packs 16 camera-facing tree impostor quads (64 vertices, 32 triangles)
// into every 64-thread workgroup with zero barriers, zero groupshared state, and zero vertex/meshlet fetches.
// DEPTH_ONLY and G-Buffer paths run identical clip-space math for bit-exact ReadEqual depth tests.

StructuredBuffer<SurvivingInstance> impostor_instances : register(t98);

struct MeshPrimitive
{
    bool cull_primitive : SV_CullPrimitive;
};

[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void main_ms(
    uint3 group_id  : SV_GroupID,
    uint3 thread_id : SV_GroupThreadID,
    out indices uint3 out_indices[IMPOSTOR_BATCH_SIZE * 2u],
#ifdef DEPTH_ONLY
    out vertices depth_mesh_vertex out_vertices[IMPOSTOR_BATCH_SIZE * 4u],
#else
    out vertices gbuffer_indirect_vertex out_vertices[IMPOSTOR_BATCH_SIZE * 4u],
#endif
    out primitives MeshPrimitive out_primitives[IMPOSTOR_BATCH_SIZE * 2u]
)
{
    const uint tid        = thread_id.x;
    const uint group_idx  = group_id.x + group_id.y * MESH_GRID_WIDTH;
#ifdef DEPTH_LATE
    const uint list_base  = cull_phase[CULL_PHASE_IMPOSTOR_BASE];
    const uint list_count = cull_phase[CULL_PHASE_IMPOSTOR_COUNT + 1u];
#else
    const uint list_base  = 0u;
    const uint list_count = cull_phase[CULL_PHASE_IMPOSTOR_COUNT];
#endif
    const uint first_quad = group_idx * IMPOSTOR_BATCH_SIZE;
    const uint quad_count = first_quad < list_count ? min(IMPOSTOR_BATCH_SIZE, list_count - first_quad) : 0u;

    SetMeshOutputCounts(quad_count * 4u, quad_count * 2u);
    if (quad_count == 0u)
    {
        return;
    }

    const uint quad_id = tid >> 2u;
    const uint corner  = tid & 3u;
    if (quad_id < quad_count)
    {
        SurvivingInstance si = impostor_instances[list_base + first_quad + quad_id];
        DrawData draw        = indirect_draw_data[si.draw_index];

        float2 input_uv = float2(
            (corner == 1u || corner == 2u) ? 1.0f : 0.0f,
            (corner >= 2u)                 ? 1.0f : 0.0f
        );

        bool is_per_instance = (draw.flags & 2u) != 0u;
        float4x4 instance    = is_per_instance
            ? pull_instance_transform(draw.instance_offset, si.instance_index)
            : float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);

        float4x4 transform          = mul(instance, draw.transform);
        float4x4 transform_previous = mul(instance, draw.transform_previous);

        uint frames         = draw.impostor_layout & 0xFFu;
        float3x3 basis      = (float3x3)transform;
        float3 center_world = mul(float4(draw.impostor_center, 1.0f), transform).xyz;
        float3 axis_scale   = float3(dot(basis[0], basis[0]), dot(basis[1], basis[1]), dot(basis[2], basis[2]));
        float3 view_local   = normalize(mul(basis, buffer_frame.camera_position - center_world) / max(axis_scale, 1e-8f));
        uint2 frame         = impostor_nearest_frame(view_local, frames);
        float3 direction    = impostor_frame_direction(frame, frames);
        float3 right;
        float3 up;
        impostor_frame_basis(direction, right, up);

        float2 offset     = (input_uv * 2.0f - 1.0f) * draw.impostor_radius;
        float3 card_local = draw.impostor_center + right * offset.x + up * offset.y;

        float3 position          = mul(float4(card_local, 1.0f), transform).xyz;
        float3 position_previous = mul(float4(card_local, 1.0f), transform_previous).xyz;
        float3 pos_relative      = position - buffer_frame.camera_position;
        float3 pos_relative_prev = position_previous - buffer_frame.camera_position_previous;

        float4 clip_pos      = mul(float4(pos_relative, 1.0f), buffer_frame.view_projection_relative);
        float4 clip_pos_prev = mul(float4(pos_relative_prev, 1.0f), buffer_frame.view_projection_previous_relative);

        float2 uv_frames = float2(frame) + 0.002f + input_uv * 0.996f;

#ifdef DEPTH_ONLY
        float lod_fade = compute_tree_impostor_fade(draw.impostor_center, draw.impostor_radius, draw.cluster_error, transform);
        depth_mesh_vertex out_v;
        out_v.position       = clip_pos;
        out_v.uv_misc        = float4(uv_frames, -1.0f - (float)si.draw_index, (lod_fade > 0.0f && lod_fade < 1.0f) ? lod_fade : 2.0f);
        out_v.material_index = draw.material_index;
        out_v.view_id        = 0u;
        out_vertices[tid]    = out_v;
#else
        float3x3 cofactor;
        cofactor[0]       = cross(basis[1], basis[2]);
        cofactor[1]       = cross(basis[2], basis[0]);
        cofactor[2]       = cross(basis[0], basis[1]);
        float orientation = dot(basis[0], cofactor[0]) < 0.0f ? -1.0f : 1.0f;

        float3 normal_world  = normalize(mul(direction, cofactor) * orientation);
        float3 card_tangent  = mul(right, basis);
        float3 tangent_world = normalize(card_tangent - normal_world * dot(card_tangent, normal_world));

        gbuffer_indirect_vertex out_v;
        out_v.position          = clip_pos;
        out_v.position_previous = clip_pos_prev;
        out_v.normal            = normal_world;
        out_v.tangent           = tangent_world;
        out_v.uv_misc           = float4(uv_frames, -1.0f, (float)si.instance_index);
        out_v.width_percent     = 0.0f;
        out_v.draw_index        = si.draw_index;
        out_v.view_id           = 0u;
        out_v.ocean_world_xz    = position.xz;
        out_vertices[tid]       = out_v;
#endif
    }

    if (tid < quad_count * 2u)
    {
        const uint q    = tid >> 1u;
        const uint half = tid & 1u;
        const uint b    = q * 4u;
        MeshPrimitive prim;
        prim.cull_primitive = false;
        out_primitives[tid] = prim;
        out_indices[tid]    = half == 0u ? uint3(b + 0u, b + 2u, b + 1u) : uint3(b + 0u, b + 3u, b + 2u);
    }
}
