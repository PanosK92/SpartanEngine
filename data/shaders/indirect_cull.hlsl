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

// gpu-driven meshlet cull, phase b of the two-phase cull, dispatched indirect with one workgroup per instance that survived phase a (instance_cull.hlsl)
// each workgroup rebuilds its instance's world transform once into groupshared then expands that instance's meshlets across its threads
// per meshlet: sphere side-frustum, cone backface (sqrt-free), hi-z via analytical sphere projection, skinned instances keep every meshlet since deformation invalidates the static bounds
// survivors are wave-aggregated into meshlet_instances and triangle_dispatch_args.group_count_x is bumped by the same amount in lockstep
// flags bit 0 skinned, bit 1 per_instance, bit 3 two_sided (read by the triangle pass)
//
// the old single-phase kernel emitted one task per (meshlet x instance) which exploded into millions of threads for consolidated forest entities
// the instance prepass collapses that to one workgroup per visible instance, only the meshlets of survivors are ever projected here
// frustum + hi-z helpers live in common_culling.hlsl, the occluder hi-z arrives on the tex slot

// signed-byte cone unpack, dxc lowers this to a single byte_address load and four shifts
int4 unpack_cone_axis_cutoff(uint packed)
{
    int4 v = int4(packed << 24, packed << 16, packed << 8, packed);
    return v >> 24;
}

// phase b of the two-phase gpu cull, dispatched indirect with one workgroup per surviving instance from phase a
// the workgroup rebuilds that instance's world transform once into groupshared then expands its meshlets across the
// threads, running per-meshlet side-frustum + backface cone + analytical hi-z and wave-compacting survivors into
// meshlet_instances while bumping triangle_dispatch_args.group_count_x
// the heavy per-(meshlet x instance) cull-task explosion of the old single-phase path is gone, only the meshlets of
// the instances phase a kept are ever touched here
groupshared uint     gs_draw_index;
groupshared uint     gs_instance_index;
groupshared DrawData gs_draw;
groupshared float4x4 gs_world;
groupshared float    gs_scale_max;
groupshared bool     gs_skinned;
groupshared bool     gs_two_sided;
groupshared uint     gs_meshlet_offset;
groupshared uint     gs_meshlet_count;

[numthreads(64, 1, 1)]
void main_cs(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID)
{
    uint surv_index = group_id.x;
    uint lid        = group_thread_id.x;

    float max_mip_level         = pass_get_f4_value().x;
    uint  max_meshlet_instances = (uint)pass_get_f4_value().y;

    // wave-uniform, the compiler scalarizes the plane extraction across the wave
    float4 plane_l, plane_r, plane_b, plane_t;
    get_frustum_side_planes(plane_l, plane_r, plane_b, plane_t);

    // header load, one thread rebuilds the per-instance state into groupshared, all threads consume after the barrier
    if (lid == 0)
    {
        SurvivingInstance si  = surviving_instances[surv_index];
        gs_draw_index         = si.draw_index;
        gs_instance_index     = si.instance_index;
        gs_draw               = indirect_draw_data[si.draw_index];
        bool is_per_instance  = (gs_draw.flags & 2u) != 0u;
        gs_world              = is_per_instance
            ? mul(pull_instance_transform(gs_draw.instance_offset, si.instance_index), gs_draw.transform)
            : gs_draw.transform;
        gs_scale_max          = max_world_scale(gs_world);
        gs_skinned            = (gs_draw.flags & 1u) != 0u;
        gs_two_sided          = (gs_draw.flags & 8u) != 0u;
        gs_meshlet_offset     = gs_draw.lod_meshlet_offset;
        gs_meshlet_count      = gs_draw.lod_meshlet_count;
    }
    GroupMemoryBarrierWithGroupSync();

    uint meshlet_count = gs_meshlet_count;
    uint iterations    = (meshlet_count + 63u) / 64u; // uniform across the workgroup so the wave ops below stay convergent

    for (uint it = 0u; it < iterations; it++)
    {
        uint m                 = lid + it * 64u;
        bool is_visible        = false;
        MeshletInstance out_mi = (MeshletInstance)0;

        if (m < meshlet_count)
        {
            uint global_meshlet   = gs_meshlet_offset + m;
            out_mi.draw_index     = gs_draw_index;
            out_mi.meshlet_index  = global_meshlet;
            out_mi.instance_index = gs_instance_index;
            out_mi.padding0       = 0u;

            if (gs_skinned)
            {
                // skinned bounds are invalid after deformation, the instance already passed phase a so keep every meshlet
                is_visible = true;
            }
            else
            {
                MeshletBounds mb    = meshlet_bounds[global_meshlet];
                // dequantize the compressed bounds against the lod aabb the cpu packer used in build_meshlets
                float3 center_local = meshlet_decode_center(mb, gs_draw.lod_aabb_min, gs_draw.lod_aabb_extent);
                float  radius_local = meshlet_decode_radius(mb, gs_draw.lod_aabb_diag);
                float3 center_world = mul(float4(center_local, 1.0f), gs_world).xyz;
                float  radius_world = radius_local * gs_scale_max;

                is_visible = sphere_in_side_planes(center_world, radius_world, plane_l, plane_r, plane_b, plane_t);

                // per-meshlet backface cone, skipped for two-sided materials and degenerate cones, sqrt-free form
                if (is_visible && !gs_two_sided)
                {
                    int4 cone = unpack_cone_axis_cutoff(mb.cone_axis_cutoff);
                    if (cone.w < 127)
                    {
                        float3 cone_axis_local = float3(cone.xyz) / 127.0f;
                        float  cone_cutoff     = float(cone.w) / 127.0f;
                        float3 cone_axis_world = normalize(mul(cone_axis_local, (float3x3)gs_world));
                        float3 to_meshlet      = center_world - buffer_frame.camera_position;
                        float  to_meshlet_sq   = dot(to_meshlet, to_meshlet);
                        float  lhs             = dot(to_meshlet, cone_axis_world);
                        float  lhs_off         = lhs - radius_world;
                        if (lhs_off > 0.0f && lhs_off * lhs_off >= cone_cutoff * cone_cutoff * to_meshlet_sq)
                            is_visible = false;
                    }
                }

                // per-meshlet hi-z, analytical sphere projection
                if (is_visible)
                    is_visible = sphere_hiz_visible(tex, center_world, radius_world, max_mip_level);
            }
        }

        // wave-aggregated atomic, the first lane reserves a contiguous range for the whole wave
        uint wave_count = WaveActiveCountBits(is_visible);
        uint lane_off   = WavePrefixCountBits(is_visible);
        uint wave_base  = 0u;
        uint overflow_u = 0u;
        if (WaveIsFirstLane() && wave_count > 0u)
        {
            InterlockedAdd(triangle_dispatch_args[0].group_count_x, wave_count, wave_base);
            overflow_u = (wave_base + wave_count) > max_meshlet_instances ? 1u : 0u;
        }
        wave_base  = WaveReadLaneFirst(wave_base);
        overflow_u = WaveReadLaneFirst(overflow_u);

        // clamp group_count_x to the survivor cap, the triangle pass guards on mi_idx but a runaway dispatch can still hit the hw ceiling
        if (overflow_u != 0u && WaveIsFirstLane())
            InterlockedMin(triangle_dispatch_args[0].group_count_x, max_meshlet_instances);

        if (is_visible)
        {
            uint slot = wave_base + lane_off;
            if (slot < max_meshlet_instances)
                meshlet_instances[slot] = out_mi;
        }
    }
}
