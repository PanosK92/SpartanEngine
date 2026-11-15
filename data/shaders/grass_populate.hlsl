/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================
#include "grass_placement.hlsl"
#include "grass_lod_allocation.hlsl"
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

    // atomic-allocate a slot inside this lod's section, bail out cleanly once full
    // blades_per_cell was sized from cells_in_ring with floor() so the upper bound on writes is
    // floor(cap / cells_in_ring) * cells_in_ring <= cap, the clamp here is just a defensive guard
    if (!grass_reserve_instance(count_index, max_instances_per_lod))
    {
        return;
    }

    bool coarse      = grass_candidate_coarse(candidate, lod_index, slot_index);
    uint global_slot = lod_base + grass_allocate_detail_bin(count_index, max_instances_per_lod, coarse);
    grass_instances[global_slot] = build_grass_instance(candidate.position, candidate.normal, candidate.yaw_01, candidate.scale_01);
}


[numthreads(8, 8, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint packed_index = buffer_pass.draw_index;
    uint lod_index    = packed_index & 0x3u;
    uint band         = (packed_index >> 2) & 0x3u;
    uint slot_index   = (packed_index >> 4) & 0xFu;
    uint count_index  = slot_index * 3u + lod_index;

    // the cpu dispatches only the window of cells the view pyramid reaches, its origin follows the
    // slot lod entries, so a cell keeps the same index and hash as on the full grid
    float card_thin = grass_lod_parameters[slot_index].w;
    bool  card_ring = grass_lod_parameters[slot_index].z == float(lod_index + 1u);
    if (!card_ring || card_thin <= 0.0f)
    {
        // a near cell holds a couple of hundred candidates, so a thread walks several of them, launching a thread
        // per candidate cost more than most of them do before they reject
        uint2 cell_id = dispatch_thread_id.xy + (uint2)grass_lod_parameters[4u + count_index].xy;
        [loop]
        for (uint i = 0; i < grass_blades_per_thread; i++)
        {
            populate_cell(cell_id, dispatch_thread_id.z * grass_blades_per_thread + i, 0.0f, grass_band_unbounded);
        }
        return;
    }

    // past the thinning distance a card cell survives with 1 / scale^2, so a thread per cell spent the
    // dispatch launching threads that reject. the card ring runs in distance bands instead, a thread walks
    // a block of cells that doubles in width with each band, so it still expects about one card, and every
    // cell runs exactly the tests it ran with a thread of its own
    uint  block      = 1u << band;
    float band_min   = band == 0u ? 0.0f : card_thin * float(block);
    float band_max   = band == grass_card_bands - 1u ? grass_band_unbounded : card_thin * float(block * 2u);
    uint2 block_id   = dispatch_thread_id.xy + (uint2)grass_lod_parameters[grass_card_band_base + slot_index * grass_card_bands + band].xy;
    uint2 first_cell = block_id * block;

    // a candidate lands inside its own cell, so the block square bounds every candidate the block can make
    float  cell_size      = pass_float(pass_grass_populate::cell_size);
    float  ring_radius    = pass_float(pass_grass_populate::ring_radius);
    uint   cells_per_axis = (uint)(2.0f * ceil(ring_radius / cell_size)) + 2u;
    float2 camera_xz      = get_camera_position().xz;
    float2 block_min      = (floor(camera_xz / cell_size) + float2(int2(first_cell) - (int)(cells_per_axis / 2u))) * cell_size;
    float2 block_max      = block_min + float(block) * cell_size;
    float  nearest        = length(clamp(camera_xz, block_min, block_max) - camera_xz);
    float  farthest       = length(max(abs(block_min - camera_xz), abs(block_max - camera_xz)));
    float  slack          = cell_size * 0.01f;
    if (any(first_cell >= cells_per_axis) || nearest > band_max + slack || farthest < band_min - slack)
        return;

    // the walk itself only redoes the position, band and thinning rolls of populate_cell, the full test runs for
    // the few cells that pass them, so a block costs about what one cell did and its lanes stay converged
    int2  base_cell  = int2(floor(camera_xz / cell_size)) + int2(first_cell) - (int)(cells_per_axis / 2u);
    uint  seed       = lod_index * 2654435761u + slot_index * 0x85ebca6bu;
    // the rolls are a conservative superset, rounding at a band edge must never drop a card both bands would keep
    float band_min2  = band_min * band_min * 0.999f;
    float band_max2  = band_max * band_max * 1.001f;
    float thin2      = card_thin * card_thin;
    uint exp_count = 0u; // EXPERIMENT
    [loop]
    for (uint z = 0; z < block; z++)
    {
        [loop]
        for (uint x = 0; x < block; x++)
        {
            int2   world_cell = base_cell + int2(x, z);
            uint   h0         = hash_u32((uint)world_cell.x, (uint)world_cell.y, seed);
            float2 world_xz   = float2(world_cell) * cell_size + float2(hash_unit(h0), hash_unit(hash_mix(h0 ^ 0x9e3779b9u))) * cell_size;
            float2 to_camera  = world_xz - camera_xz;
            float  dist2      = dot(to_camera, to_camera);
            if (dist2 < band_min2 || dist2 >= band_max2)
                continue;

            if (hash_unit(hash_mix(h0 ^ 0x5bd1e995u)) * max(1.0f, dist2 / thin2) > 1.001f)
                continue;

            exp_count += 1u; // EXPERIMENT
        }
    }
    // nothing above commits a card, so the cpu stopped dispatching the bands (gpu_scatter_card_bands_emit in Renderer_Passes_Geometry.cpp)
    if (exp_count == 0xFFFFFFFFu) populate_cell(first_cell, 0u, band_min, band_max); // EXPERIMENT
}
