/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
#include "grass_lod_allocation.hlsl"
//====================

// gpu procedural grass indirect args builder
//
// reads the per-lod atomic counters bumped by grass_populate.hlsl and clamps them into the
// instance_count field of the per-lod IndirectDrawArgs entries. the other fields (index_count,
// first_index, vertex_offset, first_instance) were baked once on the cpu in EnableGpuScatter
// from the grass mesh's per-lod offsets in the global geometry buffer.
//
// the args buffer holds one entry per scatter slot per lod laid out slot major, the raster draw calls
// offset into it by (slot * lod_count + lod) * sizeof(IndirectDrawArgs) to fetch theirs. this runs once
// per slot, the cpu pushes the caps of that slot and the index of its first entry.
//
// push constant layout (PassBufferData.values):
//   values[0].xyz = per-lod max_instances cap, one float per lod (must match the populate shader's
//                   per-lod cap that the cpu pushed into values[0].w of grass_populate.hlsl)
//   values[0].w   = lod_count
//   draw_index    = the first args entry of this slot

[numthreads(8, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint lod_count = (uint)buffer_pass.values[0].w;
    uint slot_base = buffer_pass.draw_index;

    uint lod = dispatch_thread_id.x;
    if (lod >= lod_count)
        return;

    // pull the matching per-lod cap, the populate dispatch may have raced past it
    float caps[3]              = { buffer_pass.values[0].x, buffer_pass.values[0].y, buffer_pass.values[0].z };
    uint  max_instances_per_lod = (uint)caps[lod];

    // clamp the atomic counter so the raster never tries to draw more instances than the range can hold
    uint entry          = slot_base + lod;
    // Total allocation is capped before either dense draw list is allocated.
    // The two lists grow from opposite ends of the original instance range.
    uint2 counts = grass_detail_counts(entry, max_instances_per_lod);

    // only the instance_count is dynamic, the rest of the args stay frozen at the values the cpu wrote
    grass_indirect_args[entry].instance_count = counts.x;
    grass_indirect_args[entry + grass_ring_count].instance_count = counts.y;
}
