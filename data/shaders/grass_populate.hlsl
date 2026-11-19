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
void populate_cell(uint2 cell_id, uint blade_index, float band_min, float band_max)
{
    GrassPlacementParams params = grass_placement_params_from_pass();
    GrassCandidate candidate;
    if (!grass_place_candidate(params, cell_id, blade_index, band_min, band_max, candidate))
    {
        return;
    }

    uint lod_base              = params.lod_base;
    uint max_instances_per_lod = params.max_instances_per_lod;
    uint lod_index             = params.packed_index & 0x3u;
    uint slot_index            = (params.packed_index >> 4) & 0xFu;
    uint count_index           = slot_index * 3u + lod_index;

    uint base = 0u;
    uint count = WaveActiveCountBits(true);
    uint offset = WavePrefixCountBits(true);
    if (WaveIsFirstLane())
        InterlockedAdd(grass_count[count_index], count, base);
    uint ring_slot = WaveReadLaneFirst(base) + offset;
    if (ring_slot >= max_instances_per_lod) return;
    uint global_slot = lod_base + ring_slot;
    grass_instances[global_slot] = build_grass_instance(candidate.position, candidate.normal, candidate.yaw_01, candidate.scale_01);
}


[numthreads(8, 8, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint packed_index = buffer_pass.draw_index;
    uint lod_index    = packed_index & 0x3u;
    uint slot_index   = (packed_index >> 4) & 0xFu;
    uint count_index  = slot_index * 3u + lod_index;

    // the cpu dispatches only the window of cells the view pyramid reaches, its origin follows the
    // slot lod entries, so a cell keeps the same index and hash as on the full grid
    uint2 cell_id = dispatch_thread_id.xy + (uint2)grass_lod_parameters[4u + count_index].xy;
    [loop]
    for (uint i = 0; i < grass_blades_per_thread; i++)
    {
        populate_cell(cell_id, dispatch_thread_id.z * grass_blades_per_thread + i, 0.0f, grass_band_unbounded);
    }
}
