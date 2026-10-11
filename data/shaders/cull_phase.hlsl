/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// single thread bookkeeping between the early and late cull, the late cull appends to the same survivor lists
// snapshot: record where the early survivors end, before the late cull starts appending
// late meshlets: the meshlets the late cull appended become their own dispatch (vs triangle cull, mesh draw)
// late triangles: the triangles the late triangle cull appended become their own draw

uint appended(uint total, uint base)
{
    return max(total, base) - base;
}

[numthreads(1, 1, 1)]
void main_cs()
{
    uint mode = pass_uint(pass_cull_phase::mode);

    if (mode == CULL_PHASE_MODE_SNAPSHOT)
    {
        cull_phase[CULL_PHASE_MESHLET_BASE_OPAQUE]  = triangle_dispatch_args[0].group_count_x;
        cull_phase[CULL_PHASE_MESHLET_BASE_ALPHA]   = triangle_dispatch_args[1].group_count_x;
        cull_phase[CULL_PHASE_TRIANGLE_BASE_OPAQUE] = indirect_draw_args[0].index_count / 3u;
        cull_phase[CULL_PHASE_TRIANGLE_BASE_ALPHA]  = indirect_draw_args[1].index_count / 3u;
    }
    else if (mode == CULL_PHASE_MODE_LATE_MESHLETS)
    {
        triangle_dispatch_args[2].group_count_x = appended(triangle_dispatch_args[0].group_count_x, cull_phase[CULL_PHASE_MESHLET_BASE_OPAQUE]);
        triangle_dispatch_args[3].group_count_x = appended(triangle_dispatch_args[1].group_count_x, cull_phase[CULL_PHASE_MESHLET_BASE_ALPHA]);
    }
    else
    {
        indirect_draw_args[2].index_count = appended(indirect_draw_args[0].index_count / 3u, cull_phase[CULL_PHASE_TRIANGLE_BASE_OPAQUE]) * 3u;
        indirect_draw_args[3].index_count = appended(indirect_draw_args[1].index_count / 3u, cull_phase[CULL_PHASE_TRIANGLE_BASE_ALPHA]) * 3u;
    }
}
