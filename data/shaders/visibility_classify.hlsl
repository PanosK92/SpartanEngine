/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// 8x8 tile classification for the visibility buffer material resolve:
//   - masks out pixels already shaded by gpu scatter (when scatter ran first)
//   - clears sky/empty g-buffer pixels when scatter did not clear the g-buffer
//   - compacts tiles containing opaque, alpha-tested, or impostor pixels into 3 indirect dispatch lists

[[vk::image_format("unknown")]] RWTexture2D<float4> rw_gbuffer_albedo   : register(u80);
[[vk::image_format("rg32ui")]]  RWTexture2D<uint2>  rw_gbuffer_normal   : register(u81);
[[vk::image_format("r32ui")]]   RWTexture2D<uint>   rw_gbuffer_material : register(u82);
[[vk::image_format("unknown")]] RWTexture2D<float4> rw_gbuffer_velocity : register(u83);
[[vk::image_format("unknown")]] RWTexture2D<float4> rw_gbuffer_emissive : register(u84);
[[vk::image_format("r32ui")]]   RWTexture2D<uint>   rw_visibility       : register(u85);

RWStructuredBuffer<IndirectDispatchArgs> visibility_tile_args : register(u86);
RWStructuredBuffer<uint>                 visibility_tiles     : register(u87);

groupshared uint gs_category_mask;

[numthreads(VIS_TILE_SIZE, VIS_TILE_SIZE, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID, uint group_index : SV_GroupIndex)
{
    if (group_index == 0u)
    {
        gs_category_mask = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 resolution    = (uint2)get_render_resolution_active();
    uint2 pixel         = dispatch_id.xy;
    uint  cat_bit       = 0u;
    bool  scatter_clear = pass_bool(pass_visibility::scatter_cleared);
    bool  clear_gbuffer = pass_bool(pass_visibility::clear_gbuffer);
    uint  meshlet_half  = pass_uint(pass_visibility::meshlet_half_capacity);

    if (pixel.x < resolution.x && pixel.y < resolution.y)
    {
        uint vis_id = rw_visibility[pixel];

        // gpu scatter drew before resolve and owns every pixel it shaded (packed material is always non-zero)
        if (scatter_clear && vis_id != 0u && rw_gbuffer_material[pixel] != 0u)
        {
            vis_id               = 0u;
            rw_visibility[pixel] = 0u;
        }

        if (vis_id == 0u)
        {
            if (clear_gbuffer)
            {
                rw_gbuffer_albedo[pixel]   = float4(0.0f, 0.0f, 0.0f, 0.0f);
                rw_gbuffer_normal[pixel]   = uint2(0u, 0u);
                rw_gbuffer_material[pixel] = 0u;
                rw_gbuffer_velocity[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
                rw_gbuffer_emissive[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            }
        }
        else if ((vis_id & VIS_FLAG_IMPOSTOR) != 0u)
        {
            cat_bit = 1u << VIS_CATEGORY_IMPOSTOR;
        }
        else if ((vis_id & VIS_FLAG_MESHLET) != 0u)
        {
            uint mi_idx   = (vis_id >> VIS_TRI_BITS) & VIS_MESHLET_MASK;
            bool is_alpha = mi_idx >= meshlet_half;
            cat_bit       = 1u << (is_alpha ? VIS_CATEGORY_ALPHA : VIS_CATEGORY_OPAQUE);
        }
    }

    uint wave_mask = WaveActiveBitOr(cat_bit);
    if (WaveIsFirstLane() && wave_mask != 0u)
    {
        InterlockedOr(gs_category_mask, wave_mask);
    }
    GroupMemoryBarrierWithGroupSync();

    if (group_index == 0u && gs_category_mask != 0u)
    {
        uint tile_stride = pass_uint(pass_visibility::tile_stride);
        uint packed_tile = (group_id.x & 0xFFFFu) | (group_id.y << 16u);

        [unroll]
        for (uint cat = 0u; cat < VIS_CATEGORY_COUNT; cat++)
        {
            if ((gs_category_mask & (1u << cat)) != 0u)
            {
                uint slot;
                InterlockedAdd(visibility_tile_args[cat].group_count_x, 1u, slot);
                if (slot < tile_stride)
                {
                    visibility_tiles[cat * tile_stride + slot] = packed_tile;
                }
            }
        }
    }
}
