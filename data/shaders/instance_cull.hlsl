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
RWStructuredBuffer<uint>              instance_visibility : register(u89);
// direct phase-a impostor survivors bypass phase-b meshlet expansion and batch 16 quads per mesh shader group
RWStructuredBuffer<SurvivingInstance> impostor_instances  : register(u95);

groupshared uint gs_visibility[INSTANCE_CULL_BATCH_SIZE / 32u];
groupshared uint gs_in_frustum[INSTANCE_CULL_BATCH_SIZE / 32u];

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
    uint  max_impostors = pass_uint(pass_instance_cull::max_impostors);
    float4 plane_l, plane_r, plane_b, plane_t;
    get_frustum_side_planes(plane_l, plane_r, plane_b, plane_t);

    bool     survives              = false;
    bool     in_frustum            = false;
    bool     drawn_early           = false;
    bool     is_impostor_batch     = false;
    bool     crossfade_impostor    = false;
    uint     crossfade_proxy_index = 0u;
    CullTask task                  = (CullTask)0;
    const uint vis_words_per_task  = INSTANCE_CULL_BATCH_SIZE / 32u;
    uint     vis_task_base         = task_index * (vis_words_per_task * 2u);
    uint     visible_word          = vis_task_base + (thread_id.x >> 5u);
    uint     frustum_word          = vis_task_base + vis_words_per_task + (thread_id.x >> 5u);
    uint     visible_bit           = 1u << (thread_id.x & 31u);

    if (task_index >= task_count) return; // uniform across the whole workgroup
#if !defined(API_WEBGPU)
    if (late)
    {
        // Uniform workgroup-level exit: if zero instances in this 64-instance batch passed
        // distance/frustum/contribution in the early pass, skip the entire task before any fetches.
        // Disabled on WebGPU because Tint flags reads of RWStructuredBuffer before the !late barrier.
        uint frustum_any = 0u;
        [unroll]
        for (uint w = 0u; w < vis_words_per_task; ++w)
        {
            frustum_any |= instance_visibility[vis_task_base + vis_words_per_task + w];
        }
        if (frustum_any == 0u) return;
    }
#endif
    if (!late && thread_id.x < vis_words_per_task)
    {
        gs_visibility[thread_id.x] = 0u;
        gs_in_frustum[thread_id.x] = 0u;
    }
    task = cull_tasks[task_index];

    // Wave-uniform task metadata hoisted once per workgroup so DXC scalarizes all draws/materials/distance roots.
    const DrawData base_draw           = indirect_draw_data[task.draw_index];
    const bool     skinned             = (base_draw.flags & 1u) != 0u;
    const bool     is_per_instance     = (base_draw.flags & 2u) != 0u;
    const bool     skip_hiz            = (base_draw.flags & 32u) != 0u;
    const bool     has_canopy          = task.canopy_draw != 0u;
    const uint     canopy_proxy_idx    = has_canopy ? ((task.canopy_draw & 0x7fffffffu) - 1u) : 0u;
    DrawData       canopy_proxy        = base_draw;
    if (has_canopy)
    {
        canopy_proxy = indirect_draw_data[canopy_proxy_idx];
    }
    const bool     base_wind_animated  = (material_parameters[base_draw.material_index].flags & (1u << 9)) != 0u;
    const bool     proxy_wind_animated = has_canopy ? ((material_parameters[canopy_proxy.material_index].flags & (1u << 9)) != 0u) : base_wind_animated;
    const float    base_max_dist       = base_draw.max_render_distance_squared > 0.0f ? sqrt(base_draw.max_render_distance_squared) : 0.0f;
    const float    proxy_max_dist      = canopy_proxy.max_render_distance_squared > 0.0f ? sqrt(canopy_proxy.max_render_distance_squared) : 0.0f;
    const float    proj_m11_res_y     = abs(buffer_frame.projection[1][1]) * get_render_resolution_active().y;
    const float    proj_m23_abs       = abs(buffer_frame.projection[2][3]);

    uint meshlet_rows = 1u;
    if (thread_id.x < task.instance_count)
    {
        // In the late pass, any instance that failed distance/frustum/contribution in the early pass of
        // this same frame cannot pass in the late pass either: reject in 1 DWORD load before any fetches.
        bool skip_late = false;
        if (late)
        {
            bool in_frustum_early = (instance_visibility[frustum_word] & visible_bit) != 0u;
            if (!in_frustum_early)
            {
                skip_late = true;
            }
            else
            {
                drawn_early = (instance_visibility[visible_word] & visible_bit) != 0u;
            }
        }

        if (!skip_late)
        {
        task.instance_index += thread_id.x;
        DrawData draw       = base_draw;
        bool wind_animated  = base_wind_animated;
        float max_dist_base = base_max_dist;

        if (late && drawn_early && (skip_hiz || skinned || (!has_canopy && task.refinement_draw == 0u && draw.geometry_meshlet_count <= 1u)))
        {
            skip_late = true;
        }

        if (!skip_late)
        {
        // per-instance world transform, non-instanced draws keep draw.transform, instanced rebuild from the packed instance buffer
        float4x4 world_xform = is_per_instance
            ? mul(pull_instance_transform(draw.instance_offset, task.instance_index), draw.transform)
            : draw.transform;
        float world_scale = max_world_scale(world_xform);

        // Choose the parent once for this canopy. All its cluster ranges use
        // the same crown sphere, including ranges outside the camera frustum.
        bool used_proxy = false;
        float3 proxy_center = 0.0f;
        float  proxy_radius = 0.0f;
        if (has_canopy)
        {
            proxy_center    = mul(float4(canopy_proxy.cluster_center, 1.0f), world_xform).xyz;
            proxy_radius    = canopy_proxy.cluster_radius * world_scale;
            float w         = mul(float4(proxy_center, 1.0f), buffer_frame.view_projection).w;
            float nearest   = w - proxy_radius * proj_m23_abs;
            float diameter  = proxy_radius * proj_m11_res_y;
            if (nearest > 0.0f && diameter <= canopy_proxy.cluster_error * nearest)
            {
                if ((task.canopy_draw & 0x80000000u) != 0u)
                {
                    skip_late = true;
                }
                else
                {
                    draw                 = canopy_proxy;
                    wind_animated        = proxy_wind_animated;
                    max_dist_base        = proxy_max_dist;
                    used_proxy           = true;
                    task.draw_index      = canopy_proxy_idx;
                    task.refinement_draw = 0u;
                    // Impostors drawn in the early pass leave zero unrendered meshlets for the late pass.
                    if (late && drawn_early && max_impostors > 0u && ((draw.flags & draw_flag_impostor) != 0u))
                    {
                        skip_late = true;
                    }
                }
            }
            else if (nearest > 0.0f && (task.canopy_draw & 0x80000000u) == 0u &&
                     (canopy_proxy.flags & draw_flag_impostor) != 0u &&
                     diameter < canopy_proxy.cluster_error * TREE_IMPOSTOR_FADE_BAND * nearest)
            {
                // Crossfade band: keep the 3D canopy active and also emit its impostor for screen-door blending
                crossfade_impostor    = (max_impostors > 0u);
                crossfade_proxy_index = canopy_proxy_idx;
            }
        }

        if (!skip_late)
        {
        // instance bounding sphere, skinned uses the cpu-updated dynamic world aabb because deformation invalidates the static lod aabb
        float3 center_world;
        float  radius_world;
        if (skinned)
        {
            Aabb aabb    = aabbs[draw.aabb_index];
            center_world = (aabb.min + aabb.max) * 0.5f;
            radius_world = length(aabb.max - aabb.min) * 0.5f;
        }
        else if (used_proxy)
        {
            center_world = proxy_center;
            radius_world = proxy_radius;
        }
        else
        {
            // The conservative range covers every resident version of these clusters.
            center_world = mul(float4(draw.cluster_center, 1.0f), world_xform).xyz;
            radius_world = draw.cluster_radius * world_scale;
        }

        const float unpadded_radius = radius_world;

        if (late)
        {
            // in_frustum_early already proved !skip_range, passes_distance, side frustum, and contribution
            // for this exact frame; only apply wind padding needed for the Hi-Z sphere test below.
            if (wind_animated)
            {
                radius_world += tree_wind_cull_padding(center_world, radius_world, world_xform[3].xyz);
            }
            in_frustum = true;
            survives   = true;
        }
        else
        {
            if (wind_animated)
            {
                radius_world += tree_wind_cull_padding(center_world, radius_world, world_xform[3].xyz);
            }

            // Frustum side planes first (4 dot products, no sqrt), then render distance, then screen contribution.
            bool passes = sphere_in_side_planes(center_world, radius_world, plane_l, plane_r, plane_b, plane_t);
            if (passes && max_dist_base > 0.0f)
            {
                float3 to_camera = center_world - buffer_frame.camera_position;
                float dist_sq    = dot(to_camera, to_camera);
                float max_dist   = max_dist_base + radius_world;
                passes           = dist_sq <= (max_dist * max_dist);
            }
            in_frustum = passes && sphere_contributes(center_world, radius_world, CULL_CONTRIBUTION_MESH_PX);
            survives   = in_frustum;
        }

        // Select an available cluster refinement independently for each instance.
        if (survives && (task.refinement_draw & 0xc0000000u) == CULL_TASK_CLUSTER_REFINEMENT)
        {
            float3 cluster_center = skinned ? mul(float4(draw.cluster_center, 1.0f), world_xform).xyz : center_world;
            float  cluster_radius = skinned ? (draw.cluster_radius * world_scale) : unpadded_radius;
            float  clip_w         = mul(float4(cluster_center, 1.0f), buffer_frame.view_projection).w;
            float  nearest        = max(1e-4f, clip_w - cluster_radius * proj_m23_abs);
            float  pixels         = 0.5f * proj_m11_res_y;
            uint first = task.refinement_draw & CULL_TASK_LEVEL_MASK;
            uint count = 1u + ((task.refinement_draw >> CULL_TASK_LEVEL_SHIFT) & 15u);
            float threshold = nearest / max(1e-8f, world_scale * pixels);
            float error = draw.cluster_error;
            for (uint j = 0; j < count && error > threshold; ++j)
            {
                task.draw_index = first + j;
                error = indirect_draw_data[task.draw_index].cluster_error;
            }
        }

        uint meshlet_count = indirect_draw_data[task.draw_index].geometry_meshlet_count;
        is_impostor_batch  = (max_impostors > 0u) && ((draw.flags & draw_flag_impostor) != 0u);

        if (task_count <= 128u && (draw.flags & 2u) == 0u && meshlet_count >= 256u)
            meshlet_rows = min((meshlet_count + 63u) / 64u, meshlet_cull_rows);
        if (survives)
        {
            if (late)
            {
                // the early cull keeps every meshlet of hi-z exempt, skinned, 1-meshlet, and impostor instances, leaving nothing behind for these
                if (drawn_early && (skip_hiz || skinned || is_impostor_batch || meshlet_count <= 1u))
                {
                    survives = false;
                }
                else
                {
                    survives = skip_hiz || sphere_hiz_visible(tex, center_world, radius_world, max_mip_level, depth_bias);
                }
            }
            else
            {
                survives = skip_hiz || !history_hiz || sphere_hiz_visible_history(tex, center_world, radius_world, max_mip_level, depth_bias);
            }
        }
        }
        }
        }
    }

    if (!late)
    {
        const uint lane_count = WaveGetLaneCount();
        if (lane_count == 32u)
        {
            uint surv_mask = WaveActiveBallot(survives).x;
            uint frus_mask = WaveActiveBallot(in_frustum).x;
            if (WaveIsFirstLane())
            {
                uint word_idx = thread_id.x >> 5u;
                instance_visibility[vis_task_base + word_idx]                      = surv_mask;
                instance_visibility[vis_task_base + vis_words_per_task + word_idx] = frus_mask;
            }
        }
        else if (lane_count == 64u)
        {
            uint4 surv_mask = WaveActiveBallot(survives);
            uint4 frus_mask = WaveActiveBallot(in_frustum);
            if (WaveIsFirstLane())
            {
                instance_visibility[vis_task_base + 0u]                      = surv_mask.x;
                instance_visibility[vis_task_base + 1u]                      = surv_mask.y;
                instance_visibility[vis_task_base + vis_words_per_task + 0u] = frus_mask.x;
                instance_visibility[vis_task_base + vis_words_per_task + 1u] = frus_mask.y;
            }
        }
        else
        {
            GroupMemoryBarrierWithGroupSync();
            if (survives)
                InterlockedOr(gs_visibility[thread_id.x >> 5u], visible_bit);
            if (in_frustum)
                InterlockedOr(gs_in_frustum[thread_id.x >> 5u], visible_bit);
            GroupMemoryBarrierWithGroupSync();
            if (thread_id.x < vis_words_per_task)
            {
                instance_visibility[vis_task_base + thread_id.x]                      = gs_visibility[thread_id.x];
                instance_visibility[vis_task_base + vis_words_per_task + thread_id.x] = gs_in_frustum[thread_id.x];
            }
        }
    }

    // impostor survivors bypass phase-b meshlet expansion and compact straight into impostor_instances
    bool impostor_survives = survives && (is_impostor_batch || (crossfade_impostor && !(late && drawn_early)));
    uint imp_count         = WaveActiveCountBits(impostor_survives);
    uint imp_off           = WavePrefixCountBits(impostor_survives);
    uint imp_base          = 0u;
    uint imp_overflow      = 0u;
    if (WaveIsFirstLane() && imp_count > 0u)
    {
        InterlockedAdd(triangle_dispatch_args[IMPOSTOR_ARGS_SLOT].group_count_x, imp_count, imp_base);
        imp_overflow = (imp_base + imp_count) > max_impostors ? 1u : 0u;
    }
    imp_base     = WaveReadLaneFirst(imp_base);
    imp_overflow = WaveReadLaneFirst(imp_overflow);
    if (imp_overflow != 0u && WaveIsFirstLane())
        InterlockedMin(triangle_dispatch_args[IMPOSTOR_ARGS_SLOT].group_count_x, max_impostors);

    if (impostor_survives)
    {
        uint imp_slot = imp_base + imp_off;
        if (imp_slot < max_impostors)
        {
            SurvivingInstance isi;
            isi.draw_index              = crossfade_impostor ? crossfade_proxy_index : task.draw_index;
            isi.instance_index          = task.instance_index;
            impostor_instances[imp_slot] = isi;
        }
    }

    // wave-aggregated compaction, the first lane reserves a contiguous range so dense visibility doesn't hammer one address
    bool regular_survives = survives && !is_impostor_batch;
    uint wave_count = WaveActiveCountBits(regular_survives);
    uint lane_off   = WavePrefixCountBits(regular_survives);
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

    if (!regular_survives)
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
