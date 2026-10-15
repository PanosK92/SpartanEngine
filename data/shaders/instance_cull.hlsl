/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================
#include "common.hlsl"
#include "common_culling.hlsl"
//============================

StructuredBuffer<DrawLod>    draw_lods             : register(t86);
RWStructuredBuffer<DrawData> indirect_draw_data_rw : register(u87);
// one bit per task lane, the early cull writes whether it drew the instance and the late cull reads it back
// a stored bit, not a replayed test, because the early cull rewrites the lod bounds the test would read
RWStructuredBuffer<uint>     instance_visibility   : register(u89);

groupshared uint gs_visibility[INSTANCE_CULL_BATCH_SIZE / 32u];

// phase a of the gpu cull, one thread per renderable instance
// rebuilds the per-instance world transform, runs distance + side-frustum + hi-z on the instance bounds and
// compacts survivors into surviving_instances while bumping instance_dispatch_args.group_count_x, phase b then
// does a DispatchIndirect over that count and expands each survivor's meshlets
// this replaces the old single-phase path that emitted one cull task per (meshlet x instance), the task count
// now scales with instance count instead of meshlets x instances so dense world-spanning entities stay cheap
// the early cull tests last frame's hi-z, the late cull this frame's early depth hi-z, both arrive on the tex slot
// frustum + hi-z helpers live in common_culling.hlsl

[numthreads(INSTANCE_CULL_BATCH_SIZE, 1, 1)]
void main_cs(uint3 group_id : SV_GroupID, uint3 thread_id : SV_GroupThreadID)
{
    uint task_index = group_id.x + group_id.y * INSTANCE_CULL_DISPATCH_WIDTH;
    uint task_count = pass_uint(pass_instance_cull::task_count);

    // wave-uniform inputs, the planes are extracted once and the compiler scalarizes the load across the wave
    float max_mip_level = pass_float(pass_instance_cull::max_hiz_mip);
    uint  max_instances = pass_uint(pass_instance_cull::max_instances);
    bool  late          = pass_bool(pass_instance_cull::late);
    bool  history_hiz   = pass_bool(pass_instance_cull::history_hiz);
    float depth_bias    = pass_float(pass_instance_cull::hiz_depth_bias);
    float4 plane_l, plane_r, plane_b, plane_t;
    get_frustum_side_planes(plane_l, plane_r, plane_b, plane_t);

    bool     survives     = false;
    bool     drawn_early  = false;
    bool     whole_instance = false;
    CullTask task         = (CullTask)0;
    uint     visible_word = task_index * (INSTANCE_CULL_BATCH_SIZE / 32u) + (thread_id.x >> 5u);
    uint     visible_bit  = 1u << (thread_id.x & 31u);

    if (task_index >= task_count) return; // uniform across the whole workgroup
    if (!late && thread_id.x < INSTANCE_CULL_BATCH_SIZE / 32u)
        gs_visibility[thread_id.x] = 0u;
    task = cull_tasks[task_index];
    if (thread_id.x < task.instance_count)
    {
        task.instance_index += thread_id.x;
        DrawData draw = indirect_draw_data[task.draw_index];

        bool skinned         = (draw.flags & 1u) != 0u;
        bool is_per_instance = (draw.flags & 2u) != 0u;
        bool skip_hiz        = (draw.flags & 32u) != 0u;
        bool has_impostor    = (draw.flags & (1u << 15)) != 0u;

        // per-instance world transform, non-instanced draws keep draw.transform, instanced rebuild from the packed instance buffer
        float4x4 world_xform = is_per_instance
            ? mul(pull_instance_transform(draw.instance_offset, task.instance_index), draw.transform)
            : draw.transform;

        // instance bounding sphere, skinned uses the cpu-updated dynamic world aabb because deformation invalidates the static lod aabb
        float3 center_world;
        float  radius_world;
        if (skinned)
        {
            Aabb aabb    = aabbs[draw.aabb_index];
            center_world = (aabb.min + aabb.max) * 0.5f;
            radius_world = length(aabb.max - aabb.min) * 0.5f;
        }
        else
        {
            // All LOD candidates must use the same sphere. Reduced bounds otherwise
            // disagree at thresholds, causing missing or simultaneously drawn LODs.
            DrawData reference = indirect_draw_data[task.lod_reference_draw_index];
            float3 center_local = reference.lod_aabb_min + reference.lod_aabb_extent * 0.5f;
            center_world        = mul(float4(center_local, 1.0f), world_xform).xyz;
            radius_world        = reference.lod_aabb_diag * 0.5f * max_world_scale(world_xform);
        }

        float lod_radius = radius_world;
        bool wind_animated = (material_parameters[draw.material_index].flags & (1u << 9)) != 0u;
        if (wind_animated)
            radius_world += tree_wind_cull_padding(center_world, radius_world, world_xform[3].xyz);

        // closest point on the instance sphere, origin distance was dropping roads and buildings
        // whose entity pivot sat hundreds of metres away from the mesh in front of the camera
        bool passes_distance = true;
        // Baked impostors persist beyond the authored mesh render-distance limit.
        if (!has_impostor && draw.max_render_distance_squared > 0.0f)
        {
            float3 to_camera = center_world - buffer_frame.camera_position;
            float dist_center = length(to_camera);
            float max_dist = sqrt(draw.max_render_distance_squared);
            passes_distance = (dist_center - radius_world) <= max_dist;
        }

        // frustum, then contribution, then hi-z, the hi-z test also rejects instances entirely behind the camera (the side planes alone do not)
        // contribution runs before hi-z because it reuses the same projection without sampling the hi-z pyramid
        survives = passes_distance
            && sphere_in_side_planes(center_world, radius_world, plane_l, plane_r, plane_b, plane_t)
            && sphere_contributes(center_world, radius_world, CULL_CONTRIBUTION_MESH_PX);

        // One task covers the entire LOD chain. Offsets account for missing mesh LODs.
        if (survives && (task.lod_draw_offsets & 0x80000000u) != 0u)
        {
            uint lod_count = (draw.flags >> 11u) & 7u;
            uint wanted_lod = has_impostor ? sphere_lod_index_impostor(center_world, lod_radius, lod_count) : sphere_lod_index(center_world, lod_radius, lod_count);
            task.draw_index += (task.lod_draw_offsets >> (wanted_lod * 3u)) & 7u;
        }

        // a non-instanced draw is a single thread, so it can rewrite its own draw to the lod its coverage wants
        // the coverage comes from the lod 0 bounds in the table, the late cull runs this again and must pick the same lod
        if (survives && (draw.flags & draw_flag_gpu_lod) != 0u)
        {
            uint lod_count     = (draw.flags >> 11u) & 7u;
            DrawLod lod0       = draw_lods[draw.impostor_texel_offset];
            float3 lod0_center = mul(float4(lod0.aabb_min + lod0.aabb_extent * 0.5f, 1.0f), world_xform).xyz;
            float lod0_radius  = lod0.aabb_diag * 0.5f * max_world_scale(world_xform);
            uint lod           = sphere_lod_index(lod0_center, lod0_radius, lod_count);
            DrawLod pick       = draw_lods[draw.impostor_texel_offset + lod];

            indirect_draw_data_rw[task.draw_index].lod_first_index        = pick.first_index;
            indirect_draw_data_rw[task.draw_index].lod_vertex_offset      = pick.vertex_offset;
            indirect_draw_data_rw[task.draw_index].lod_meshlet_offset     = pick.meshlet_offset;
            indirect_draw_data_rw[task.draw_index].lod_meshlet_count      = pick.meshlet_count;
            indirect_draw_data_rw[task.draw_index].lod_aabb_min           = pick.aabb_min;
            indirect_draw_data_rw[task.draw_index].lod_aabb_diag          = pick.aabb_diag;
            indirect_draw_data_rw[task.draw_index].lod_aabb_extent        = pick.aabb_extent;
            indirect_draw_data_rw[task.draw_index].previous_vertex_offset = pick.previous_vertex_offset;
            indirect_draw_data_rw[task.draw_index].flags                  = (draw.flags & ~(7u << 8u)) | (lod << 8u);
        }

        whole_instance = (indirect_draw_data[task.draw_index].flags & (1u << 14)) != 0u;
        if (survives)
        {
            if (late)
            {
                // the early cull keeps every meshlet of hi-z exempt and skinned instances, it left nothing behind for these
                drawn_early = (instance_visibility[visible_word] & visible_bit) != 0u;
                survives    = (skip_hiz || sphere_hiz_visible(tex, center_world, radius_world, max_mip_level, depth_bias)) && !(drawn_early && (skip_hiz || skinned || whole_instance));
            }
            else
            {
                survives = skip_hiz || !history_hiz || sphere_hiz_visible_history(tex, center_world, radius_world, max_mip_level, depth_bias);
            }
        }
    }

    if (!late)
    {
        GroupMemoryBarrierWithGroupSync();
        if (survives)
            InterlockedOr(gs_visibility[thread_id.x >> 5u], visible_bit);
        GroupMemoryBarrierWithGroupSync();
        if (thread_id.x < INSTANCE_CULL_BATCH_SIZE / 32u)
            instance_visibility[task_index * (INSTANCE_CULL_BATCH_SIZE / 32u) + thread_id.x] = gs_visibility[thread_id.x];
    }

    // wave-aggregated compaction, the first lane reserves a contiguous range so dense visibility doesn't hammer one address
    uint wave_count = WaveActiveCountBits(survives);
    uint lane_off   = WavePrefixCountBits(survives);
    uint wave_base  = 0u;
    uint overflow_u = 0u;
    if (WaveIsFirstLane() && wave_count > 0u)
    {
        InterlockedAdd(instance_dispatch_args[0].group_count_x, wave_count, wave_base);
        overflow_u = (wave_base + wave_count) > max_instances ? 1u : 0u;
    }
    wave_base  = WaveReadLaneFirst(wave_base);
    overflow_u = WaveReadLaneFirst(overflow_u);

    // clamp the phase b dispatch size, survivors past the cap drop their writes below
    if (overflow_u != 0u && WaveIsFirstLane())
        InterlockedMin(instance_dispatch_args[0].group_count_x, max_instances);

    if (!survives)
        return;

    uint slot = wave_base + lane_off;
    if (slot >= max_instances)
        return;

    SurvivingInstance si;
    si.draw_index     = task.draw_index;
    si.instance_index = task.instance_index | (drawn_early ? SURVIVING_INSTANCE_DRAWN_EARLY : 0u);
    surviving_instances[slot] = si;
}
