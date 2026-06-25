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

//= INCLUDES =================
#include "common.hlsl"
#include "common_culling.hlsl"
//============================

// phase a of the two-phase gpu cull, one thread per (renderable lod, instance) task
// rebuilds the per-instance world transform, runs distance + side-frustum + hi-z on the instance bounds and
// compacts survivors into surviving_instances while bumping instance_dispatch_args.group_count_x, phase b then
// does a DispatchIndirect over that count and expands each survivor's meshlets
// this replaces the old single-phase path that emitted one cull task per (meshlet x instance), the task count
// now scales with instance count instead of meshlets x instances so dense world-spanning entities stay cheap
// the occluder hi-z arrives on the tex slot, frustum + hi-z helpers live in common_culling.hlsl

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint task_index = dispatch_thread_id.x;
    uint task_count = (uint)pass_get_f4_value().x;

    // wave-uniform inputs, the planes are extracted once and the compiler scalarizes the load across the wave
    float max_mip_level = pass_get_f4_value().y;
    uint  max_instances = (uint)pass_get_f4_value().z;
    float4 plane_l, plane_r, plane_b, plane_t;
    get_frustum_side_planes(plane_l, plane_r, plane_b, plane_t);

    bool     survives = false;
    CullTask task     = (CullTask)0;

    if (task_index < task_count)
    {
        task          = cull_tasks[task_index];
        DrawData draw = indirect_draw_data[task.draw_index];

        bool skinned         = (draw.flags & 1u) != 0u;
        bool is_per_instance = (draw.flags & 2u) != 0u;

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
            float3 center_local = draw.lod_aabb_min + draw.lod_aabb_extent * 0.5f;
            center_world        = mul(float4(center_local, 1.0f), world_xform).xyz;
            radius_world        = draw.lod_aabb_diag * 0.5f * max_world_scale(world_xform);
        }

        // per-instance distance cull, the cpu Render::UpdateFrustumAndDistanceCulling can only test the whole renderable bbox
        // which always passes for consolidated world-spanning entities (forest trees, rocks), max_render_distance_squared == 0 disables it
        bool passes_distance = true;
        if (draw.max_render_distance_squared > 0.0f)
        {
            float3 instance_pos     = float3(world_xform._m30, world_xform._m31, world_xform._m32);
            float3 to_camera        = instance_pos - buffer_frame.camera_position;
            passes_distance         = dot(to_camera, to_camera) <= draw.max_render_distance_squared;
        }

        // frustum then hi-z, the hi-z test also rejects instances entirely behind the camera (the side planes alone do not)
        survives = passes_distance
            && sphere_in_side_planes(center_world, radius_world, plane_l, plane_r, plane_b, plane_t)
            && sphere_hiz_visible(tex, center_world, radius_world, max_mip_level);
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
    si.instance_index = task.instance_index;
    surviving_instances[slot] = si;
}
