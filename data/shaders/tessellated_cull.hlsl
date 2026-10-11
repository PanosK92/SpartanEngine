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

// gpu culling and lod for tessellated draws, one thread per draw writes its DrawIndexedIndirect args
// a culled draw keeps its args with zero instances, the depth prepass and the g-buffer draw the same args so they always agree
// the early cull tests last frame's hi-z and writes [0, n), the late cull tests this frame's early depth hi-z and
// writes [n, 2n) for the draws the early cull skipped, so every draw lands in at most one half

StructuredBuffer<TessellatedDraw>    tessellated_draws : register(t80);
StructuredBuffer<uint4>              tessellated_lods  : register(t81); // index count, first index, vertex offset, unused
RWStructuredBuffer<IndirectDrawArgs> tessellated_args  : register(u82);

// displacement pushes the surface along its normal by at most terrain_displacement_scale
static const float tessellated_displacement_padding = 1.0f;

[numthreads(64, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint draw_count = pass_uint(pass_tessellated_cull::draw_count);
    if (thread_id.x >= draw_count)
        return;

    bool late = pass_bool(pass_tessellated_cull::late);

    TessellatedDraw draw = tessellated_draws[thread_id.x];
    float3 center        = (draw.bounds_min + draw.bounds_max) * 0.5f;
    float radius         = length(draw.bounds_max - draw.bounds_min) * 0.5f + tessellated_displacement_padding;

    bool visible = true;
    if (draw.max_distance_squared > 0.0f)
    {
        visible = length(center - buffer_frame.camera_position) - radius <= sqrt(draw.max_distance_squared);
    }

    float4 plane_l, plane_r, plane_b, plane_t;
    get_frustum_side_planes(plane_l, plane_r, plane_b, plane_t);
    visible = visible && sphere_in_side_planes(center, radius, plane_l, plane_r, plane_b, plane_t);

    bool  skip_hiz    = (draw.flags & tessellated_flag_skip_hiz) != 0u;
    float max_mip     = pass_float(pass_tessellated_cull::max_hiz_mip);
    float depth_bias  = pass_float(pass_tessellated_cull::hiz_depth_bias);
    if (late)
    {
        bool drawn_early = tessellated_args[thread_id.x].instance_count != 0u;
        visible = visible && !drawn_early && (skip_hiz || sphere_hiz_visible(tex, center, radius, max_mip, depth_bias));
    }
    else if (visible && !skip_hiz && pass_bool(pass_tessellated_cull::history_hiz))
    {
        visible = sphere_hiz_visible_history(tex, center, radius, max_mip, depth_bias);
    }

    // same coverage as Render::UpdateLodIndices, the distance to the closest point of the bounds and the size of one instance
    float3 closest = clamp(buffer_frame.camera_position, draw.bounds_min, draw.bounds_max);
    uint lod       = sphere_lod_index(closest, length(draw.lod_extents), draw.lod_count);
    uint4 range    = tessellated_lods[draw.lod_offset + lod];

    IndirectDrawArgs args;
    args.index_count    = range.x;
    args.instance_count = visible ? draw.instance_count : 0u;
    args.first_index    = range.y;
    args.vertex_offset  = (int)range.z;
    args.first_instance = draw.first_instance;
    tessellated_args[thread_id.x + (late ? draw_count : 0u)] = args;
}
