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
    CullTask task         = (CullTask)0;
    uint     visible_word = task_index * (INSTANCE_CULL_BATCH_SIZE / 32u) + (thread_id.x >> 5u);
    uint     visible_bit  = 1u << (thread_id.x & 31u);

    if (task_index >= task_count) return; // uniform across the whole workgroup
    if (!late && thread_id.x < INSTANCE_CULL_BATCH_SIZE / 32u)
        gs_visibility[thread_id.x] = 0u;
    task = cull_tasks[task_index];
    uint meshlet_rows = 1u;
    if (thread_id.x < task.instance_count)
    {
        task.instance_index += thread_id.x;
        DrawData draw = indirect_draw_data[task.draw_index];

        bool skinned         = (draw.flags & 1u) != 0u;
        bool is_per_instance = (draw.flags & 2u) != 0u;
        bool skip_hiz        = (draw.flags & 32u) != 0u;

        // per-instance world transform, non-instanced draws keep draw.transform, instanced rebuild from the packed instance buffer
        float4x4 world_xform = is_per_instance
            ? mul(pull_instance_transform(draw.instance_offset, task.instance_index), draw.transform)
            : draw.transform;

        // Choose the parent once for this canopy. All its cluster ranges use
        // the same crown sphere, including ranges outside the camera frustum.
        bool skip_range = false;
        if (task.canopy_draw != 0u)
        {
            uint proxy_index = (task.canopy_draw & 0x7fffffffu) - 1u;
            DrawData proxy = indirect_draw_data[proxy_index];
            float3 center = mul(float4(proxy.cluster_center, 1.0f), world_xform).xyz;
            float radius = proxy.cluster_radius * max_world_scale(world_xform);
            float w = mul(float4(center, 1.0f), buffer_frame.view_projection).w;
            float nearest = w - radius * abs(buffer_frame.projection[2][3]);
            float diameter = radius * abs(buffer_frame.projection[1][1]) * get_render_resolution_active().y;
            if (nearest > 0.0f && diameter <= proxy.cluster_error * nearest)
            {
                skip_range = (task.canopy_draw & 0x80000000u) != 0u;
                draw = proxy;
                task.draw_index = proxy_index;
                task.refinement_draw = 0u;
            }
        }

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
            // The conservative range covers every resident version of these clusters.
            center_world = mul(float4(draw.cluster_center, 1.0f), world_xform).xyz;
            radius_world = draw.cluster_radius * max_world_scale(world_xform);
        }

        bool wind_animated = (material_parameters[draw.material_index].flags & (1u << 9)) != 0u;
        if (wind_animated)
            radius_world += tree_wind_cull_padding(center_world, radius_world, world_xform[3].xyz);

        // closest point on the instance sphere, origin distance was dropping roads and buildings
        // whose entity pivot sat hundreds of metres away from the mesh in front of the camera
        bool passes_distance = true;
        if (draw.max_render_distance_squared > 0.0f)
        {
            float3 to_camera = center_world - buffer_frame.camera_position;
            float dist_center = length(to_camera);
            float max_dist = sqrt(draw.max_render_distance_squared);
            passes_distance = (dist_center - radius_world) <= max_dist;
        }

        // frustum, then contribution, then hi-z, the hi-z test also rejects instances entirely behind the camera (the side planes alone do not)
        // contribution runs before hi-z because it reuses the same projection without sampling the hi-z pyramid
        survives = !skip_range && passes_distance
            && sphere_in_side_planes(center_world, radius_world, plane_l, plane_r, plane_b, plane_t)
            && sphere_contributes(center_world, radius_world, CULL_CONTRIBUTION_MESH_PX);

        // Select an available cluster refinement independently for each instance.
        if (survives && (task.refinement_draw & 0xc0000000u) == CULL_TASK_CLUSTER_REFINEMENT)
        {
            float3 cluster_center = mul(float4(draw.cluster_center, 1.0f), world_xform).xyz;
            float cluster_radius = draw.cluster_radius * max_world_scale(world_xform);
            float clip_w = mul(float4(cluster_center, 1.0f), buffer_frame.view_projection).w;
            float nearest = max(1e-4f, clip_w - cluster_radius * abs(buffer_frame.projection[2][3]));
            float pixels = 0.5f * get_render_resolution_active().y * abs(buffer_frame.projection[1][1]);
            uint first = task.refinement_draw & CULL_TASK_LEVEL_MASK;
            uint count = 1u + ((task.refinement_draw >> CULL_TASK_LEVEL_SHIFT) & 15u);
            float threshold = nearest / max(1e-8f, max_world_scale(world_xform) * pixels);
            float error = draw.cluster_error;
            for (uint j = 0; j < count && error > threshold; ++j)
            {
                task.draw_index = first + j;
                error = indirect_draw_data[task.draw_index].cluster_error;
            }
        }

        uint meshlet_count = indirect_draw_data[task.draw_index].geometry_meshlet_count;

        if (task_count <= 128u && (draw.flags & 2u) == 0u && meshlet_count >= 256u)
            meshlet_rows = min((meshlet_count + 63u) / 64u, meshlet_cull_rows);
        if (survives)
        {
            if (late)
            {
                // the early cull keeps every meshlet of hi-z exempt and skinned instances, it left nothing behind for these
                drawn_early = (instance_visibility[visible_word] & visible_bit) != 0u;
                survives    = (skip_hiz || sphere_hiz_visible(tex, center_world, radius_world, max_mip_level, depth_bias)) && !(drawn_early && (skip_hiz || skinned));
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
    if (meshlet_rows > 1u)
        InterlockedMax(instance_dispatch_args[0].group_count_y, meshlet_rows);
    si.draw_index     = task.draw_index | (meshlet_rows > 1u ? survivor_split_draw : 0u);
    si.instance_index = task.instance_index | (drawn_early ? SURVIVING_INSTANCE_DRAWN_EARLY : 0u);
    surviving_instances[slot] = si;
}
