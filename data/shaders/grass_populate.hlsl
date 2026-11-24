/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================
#include "grass_placement.hlsl"
//============================

// commits a placed candidate into this lod's section of grass_instances
void populate_cell(GrassCellContext cell, uint blade_index)
{
    GrassCandidate candidate;
    if (!grass_place_candidate_in_cell(cell, blade_index, candidate))
    {
        return;
    }

    uint base   = 0u;
    uint count  = WaveActiveCountBits(true);
    uint offset = WavePrefixCountBits(true);
    if (WaveIsFirstLane())
    {
        InterlockedAdd(grass_count[cell.count_index], count, base);
    }
    uint ring_slot = WaveReadLaneFirst(base) + offset;
    if (ring_slot >= cell.params.max_instances_per_lod)
    {
        return;
    }
    uint global_slot = cell.params.lod_base + ring_slot;
    grass_instances[global_slot] = build_grass_instance(candidate.position, candidate.normal, candidate.yaw_01, candidate.scale_01);
}


[numthreads(8, 8, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    GrassPlacementParams params = grass_placement_params_from_pass();
    uint packed_index           = params.packed_index;
    uint lod_index              = packed_index & 0x3u;
    uint slot_index             = (packed_index >> 4) & 0xFu;
    uint count_index            = slot_index * 3u + lod_index;

    // Early-out once this LOD ring has already reached its instance cap.
    if (grass_count[count_index] >= params.max_instances_per_lod)
    {
        return;
    }

    // the cpu dispatches only the window of cells the view pyramid reaches, its origin follows the
    // slot lod entries (4 slots + 1 vehicle entry = base 5), so a cell keeps the same index and hash as on the full grid
    uint2 cell_id    = dispatch_thread_id.xy + (uint2)grass_lod_parameters[5u + count_index].xy;
    uint first_blade = dispatch_thread_id.z * grass_blades_per_thread;

    GrassCellContext cell;
    uint max_active_blades;
    if (!grass_prepare_cell(params, cell_id, first_blade, cell, max_active_blades))
    {
        return;
    }

    uint blade_count = min(grass_blades_per_thread, max_active_blades - first_blade);
    [loop]
    for (uint i = 0; i < blade_count; i++)
    {
        populate_cell(cell, first_blade + i);
    }
}
