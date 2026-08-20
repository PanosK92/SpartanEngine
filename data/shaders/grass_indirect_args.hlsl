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

//= INCLUDES =========
#include "common.hlsl"
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
    uint instance_count = min(grass_count[entry], max_instances_per_lod);

    // only the instance_count is dynamic, the rest of the args stay frozen at the values the cpu wrote
    grass_indirect_args[entry].instance_count = instance_count;
}
