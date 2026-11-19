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

// gpu-driven shadow maps
//
// the cpu uploads one ShadowCaster per shadow casting draw, one task per 64 instances of a caster and one
// (light index, array index, is directional) entry per atlas slice, nothing in here is per slice on the cpu
//
// counters hold one uint per (slice, caster) plus the allocator at the end, the passes run in this order
//   clear  zeroes the counters
//   cull   (write = false) counts the instances of each caster that land in each slice
//   args   turns the counts into DrawIndexedIndirect args, allocates a contiguous range of the visible
//          list for every (slice, caster) and leaves the range start in the counter as a write cursor
//   cull   (write = true) runs the exact same kernel again and writes (draw data index, global instance)
//          into the range, the raster draws read it back through SV_InstanceID
// both cull passes share one compiled kernel, so the survivors they find match bit for bit

StructuredBuffer<ShadowCaster>       shadow_casters : register(t80);
StructuredBuffer<uint2>              shadow_tasks   : register(t81); // caster index, first instance
StructuredBuffer<uint4>              shadow_slices  : register(t82); // light index, array index, is directional, unused
RWStructuredBuffer<uint>             shadow_counts  : register(u83);
RWStructuredBuffer<IndirectDrawArgs> shadow_args    : register(u84);
RWStructuredBuffer<uint2>            shadow_visible : register(u85);

void get_light_frustum_planes(float4x4 vp, out float4 planes[6])
{
    // row-vector convention, the clip space planes sit on the columns, see get_frustum_side_planes
    float4 c0 = float4(vp._m00, vp._m10, vp._m20, vp._m30);
    float4 c1 = float4(vp._m01, vp._m11, vp._m21, vp._m31);
    float4 c2 = float4(vp._m02, vp._m12, vp._m22, vp._m32);
    float4 c3 = float4(vp._m03, vp._m13, vp._m23, vp._m33);
    planes[0] = c3 + c0;
    planes[1] = c3 - c0;
    planes[2] = c3 + c1;
    planes[3] = c3 - c1;
    planes[4] = c2;      // z >= 0
    planes[5] = c3 - c2; // z <= w
    [unroll]
    for (uint i = 0; i < 6; i++)
    {
        planes[i] /= max(length(planes[i].xyz), 1e-8f);
    }
}

bool shadow_instance_visible(ShadowCaster caster, uint instance_index, uint4 slice)
{
    float4 planes[6];
    get_light_frustum_planes(light_parameters[slice.x].transform[slice.y], planes);

    // directional cascades are orthographic and clamp depth, matching Light::IsBoundsInViewFrustum they ignore the depth planes
    uint plane_count = slice.z != 0u ? 4u : 6u;

    if ((caster.flags & shadow_caster_flag_world_bounds) != 0u)
    {
        float3 center = (caster.bounds_min + caster.bounds_max) * 0.5f;
        float3 extent = (caster.bounds_max - caster.bounds_min) * 0.5f;
        for (uint i = 0; i < plane_count; i++)
        {
            if (dot(planes[i].xyz, center) + planes[i].w + dot(abs(planes[i].xyz), extent) < 0.0f)
                return false;
        }
        return true;
    }

    DrawData draw      = draw_data[caster.draw_data_index];
    float4x4 world     = mul(pull_instance_transform(caster.instance_offset, instance_index), draw.transform);
    float3 center_local = (caster.bounds_min + caster.bounds_max) * 0.5f;
    float3 center       = mul(float4(center_local, 1.0f), world).xyz;
    float radius        = length(caster.bounds_max - caster.bounds_min) * 0.5f * max_world_scale(world);
    if ((caster.flags & shadow_caster_flag_wind) != 0u)
    {
        radius += tree_wind_cull_padding(center, radius, world[3].xyz);
    }

    if (caster.max_shadow_distance > 0.0f && length(center - buffer_frame.camera_position) - radius > caster.max_shadow_distance)
        return false;

    for (uint i = 0; i < plane_count; i++)
    {
        if (dot(planes[i].xyz, center) + planes[i].w < -radius)
            return false;
    }
    return true;
}

#if defined(SHADOW_CULL_CLEAR)

[numthreads(64, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint counter_count = pass_uint(pass_shadow_cull::slice_count) * pass_uint(pass_shadow_cull::caster_count) + 1u;
    if (dispatch_thread_id.x < counter_count)
    {
        shadow_counts[dispatch_thread_id.x] = 0u;
    }
}

#elif defined(SHADOW_CULL_ARGS)

[numthreads(64, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint slice_count  = pass_uint(pass_shadow_cull::slice_count);
    uint caster_count = pass_uint(pass_shadow_cull::caster_count);
    uint capacity     = pass_uint(pass_shadow_cull::visible_capacity);
    uint pair_count   = slice_count * caster_count;

    // inactive lanes keep taking part in the wave allocation with a zero count
    uint pair  = dispatch_thread_id.x;
    uint count = pair < pair_count ? shadow_counts[pair] : 0u;

    uint prefix = WavePrefixSum(count);
    uint total  = WaveActiveSum(count);
    uint base   = 0u;
    if (WaveIsFirstLane() && total > 0u)
    {
        InterlockedAdd(shadow_counts[pair_count], total, base);
    }
    base = WaveReadLaneFirst(base) + prefix;

    if (pair >= pair_count)
        return;

    // a range past the end draws nothing, the cursor at the capacity makes the write pass drop it too
    if (base + count > capacity)
    {
        count = 0u;
        base  = capacity;
    }

    uint caster_index   = pair % caster_count;
    uint slice_index    = pair / caster_count;
    ShadowCaster caster = shadow_casters[caster_index];
    bool directional    = shadow_slices[slice_index].z != 0u;

    IndirectDrawArgs args;
    args.index_count    = directional ? caster.index_count_directional : caster.index_count_local;
    args.instance_count = count;
    args.first_index    = directional ? caster.first_index_directional : caster.first_index_local;
    args.vertex_offset  = directional ? caster.vertex_offset_directional : caster.vertex_offset_local;
    args.first_instance = base;
    shadow_args[pair]   = args;

    shadow_counts[pair] = base;
}

#else

// one group per (task, slice), a task never spans casters so the whole group shares one counter
[numthreads(shadow_cull_group_size, 1, 1)]
void main_cs(uint3 group_id : SV_GroupID, uint3 thread_id : SV_GroupThreadID)
{
    uint task_index   = group_id.x + group_id.y * INSTANCE_CULL_DISPATCH_WIDTH;
    uint slice_index  = group_id.z;
    uint caster_count = pass_uint(pass_shadow_cull::caster_count);
    if (task_index >= pass_uint(pass_shadow_cull::task_count))
        return; // uniform across the group

    uint2 task          = shadow_tasks[task_index];
    ShadowCaster caster = shadow_casters[task.x];
    uint instance_index = task.y + thread_id.x;
    bool visible        = instance_index < caster.instance_count && shadow_instance_visible(caster, instance_index, shadow_slices[slice_index]);

    uint pair        = slice_index * caster_count + task.x;
    uint wave_count  = WaveActiveCountBits(visible);
    uint lane_offset = WavePrefixCountBits(visible);
    uint base        = 0u;
    if (WaveIsFirstLane() && wave_count > 0u)
    {
        InterlockedAdd(shadow_counts[pair], wave_count, base);
    }

    if (!pass_bool(pass_shadow_cull::write))
        return;

    base = WaveReadLaneFirst(base);
    if (!visible)
        return;

    // clamp to the range the args pass allocated, a write can then never land in another caster's range
    IndirectDrawArgs args = shadow_args[pair];
    uint slot             = base + lane_offset;
    if (slot >= args.first_instance + args.instance_count)
        return;

    shadow_visible[slot] = uint2(caster.draw_data_index, caster.instance_offset + instance_index);
}

#endif
