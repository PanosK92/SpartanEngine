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

// dispatched as one thread group per cluster, sv_groupid is the 3d cluster id
// each thread tests a stride of lights against the cluster aabb in view space, survivors
// are appended to a groupshared list which is then written into the global indices buffer
// at a fixed offset of cluster_id * CLUSTER_MAX_LIGHTS

#include "common.hlsl"
#include "light_cluster.hlsl"

#define THREADS_PER_GROUP 64

groupshared uint  gs_count;
groupshared uint  gs_indices[CLUSTER_MAX_LIGHTS];
groupshared float3 gs_aabb_min;
groupshared float3 gs_aabb_max;

// tests a light against a cluster aabb in view space
// point and area use a sphere overlap, spot uses a real cone overlap so the cone does not leak sideways
bool light_intersects_cluster(LightParameters light, float3 aabb_min, float3 aabb_max)
{
    bool is_spot = (light.flags & uint(1U << 2)) != 0;
    bool is_area = (light.flags & uint(1U << 6)) != 0;

    if (is_spot)
    {
        float angle    = max(light.angle, 1e-3f);
        float cos_half = cos(angle);
        float sin_half = sin(angle);

        float3 apex_view = world_to_view(light.position, true);
        // light.direction is already unit length on the cpu side, only normalize after the view transform
        // in case the view matrix carries non uniform scale, this avoids a redundant normalize per light
        float3 dir_view  = normalize(world_to_view(light.direction, false));

        return cone_intersects_aabb(apex_view, dir_view, light.range, cos_half, sin_half, aabb_min, aabb_max);
    }

    float3 center_ws = light.position;
    float  radius    = light.range;

    if (is_area)
    {
        // wrap the rectangle with a sphere that also covers the range falloff
        float half_diag = 0.5f * sqrt(light.area_width * light.area_width + light.area_height * light.area_height);
        radius          = light.range + half_diag;
    }

    float3 center_view = world_to_view(center_ws, true);
    return sphere_intersects_aabb(center_view, radius, aabb_min, aabb_max);
}

[numthreads(THREADS_PER_GROUP, 1, 1)]
void main_cs(uint3 cluster_id : SV_GroupID, uint thread_index : SV_GroupIndex)
{
    // thread 0 seeds shared state and the aabb, the rest wait at the barrier
    if (thread_index == 0)
    {
        gs_count = 0;
        cluster_view_space_aabb(cluster_id, gs_aabb_min, gs_aabb_max);
    }
    GroupMemoryBarrierWithGroupSync();

    uint flat_id     = cluster_flat(cluster_id);
    uint light_count = buffer_frame.cluster_light_count;

    // skip slot 0, that is the directional sun which is always evaluated outside the grid
    for (uint i = thread_index + 1u; i < light_count; i += THREADS_PER_GROUP)
    {
        LightParameters light = light_parameters[i];

        // a directional light has no spatial bound, the engine always reserves slot 0 for it
        // but we still guard against any extra directionals just in case
        if ((light.flags & uint(1U << 0)) != 0)
            continue;

        // intensity zero lights produce no radiance, drop them so they do not fill cluster slots
        if (light.intensity <= 0.0f)
            continue;

        if (!light_intersects_cluster(light, gs_aabb_min, gs_aabb_max))
            continue;

        uint slot;
        InterlockedAdd(gs_count, 1u, slot);
        if (slot < CLUSTER_MAX_LIGHTS)
        {
            gs_indices[slot] = i;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    uint raw_count   = gs_count;
    uint final_count = min(raw_count, (uint)CLUSTER_MAX_LIGHTS);

    // stable contents: bitonic-sort the surviving indices ascending so the set is deterministic
    // across frames regardless of warp scheduling, only sorts the actually populated lanes
    if (final_count > 1u)
    {
        // pad the upper lanes with a sentinel so the sort leaves them alone, sort across the full lds array
        for (uint pad = final_count + thread_index; pad < (uint)CLUSTER_MAX_LIGHTS; pad += THREADS_PER_GROUP)
        {
            gs_indices[pad] = 0xFFFFFFFFu;
        }
        GroupMemoryBarrierWithGroupSync();

        for (uint k = 2u; k <= (uint)CLUSTER_MAX_LIGHTS; k <<= 1u)
        {
            for (uint j = k >> 1u; j > 0u; j >>= 1u)
            {
                for (uint idx = thread_index; idx < (uint)CLUSTER_MAX_LIGHTS; idx += THREADS_PER_GROUP)
                {
                    uint ixj = idx ^ j;
                    if (ixj > idx)
                    {
                        bool ascending = (idx & k) == 0u;
                        uint a = gs_indices[idx];
                        uint b = gs_indices[ixj];
                        if ((ascending && a > b) || (!ascending && a < b))
                        {
                            gs_indices[idx] = b;
                            gs_indices[ixj] = a;
                        }
                    }
                }
                GroupMemoryBarrierWithGroupSync();
            }
        }
    }

    // overflow telemetry, bumped once per overflowing cluster so the cpu can warn the user
    if (thread_index == 0 && raw_count > (uint)CLUSTER_MAX_LIGHTS)
    {
        uint dummy;
        InterlockedAdd(cluster_stats[0], 1u, dummy);
    }

    // write the grid entry, count is clamped to the per cluster cap
    uint base_index = flat_id * (uint)CLUSTER_MAX_LIGHTS;

    if (thread_index == 0)
    {
        cluster_light_grid[flat_id] = uint2(base_index, final_count);
    }

    // each thread flushes its share of the local list to global memory
    for (uint w = thread_index; w < final_count; w += THREADS_PER_GROUP)
    {
        cluster_light_indices[base_index + w] = gs_indices[w];
    }
}
