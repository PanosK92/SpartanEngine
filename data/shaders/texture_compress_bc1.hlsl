/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// gpu bc1 texture compression using amd compressonator kernels
// bc1 encodes rgb into 8 bytes per 4x4 block (no alpha)
// each 64-thread group compresses 4 bc blocks

#ifndef ASPM_HLSL
#define ASPM_HLSL
#endif

#include "compressonator/bcn_common_kernel.h"
#include "common_resources_buffers.hlsl"
#include "common_resources_gpu_driven.hlsl"

uint  get_num_block_x()      { return pass_uint(pass_texture_compress::block_count_x); }
uint  get_num_total_blocks() { return pass_uint(pass_texture_compress::block_count); }
float get_quality()          { return pass_float(pass_texture_compress::quality); }
uint  get_input_mip_offset() { return pass_uint(pass_texture_compress::input_offset); }
uint  get_output_offset()    { return pass_uint(pass_texture_compress::output_offset); }
uint  get_mip_width()        { return pass_uint(pass_texture_compress::mip_width); }
uint  get_mip_height()       { return pass_uint(pass_texture_compress::mip_height); }
uint  get_groups_per_row()   { return pass_uint(pass_texture_compress::groups_per_row); }

float4 unpack_rgba8(uint packed)
{
    return float4(
        float((packed      ) & 0xFFu) / 255.0,
        float((packed >>  8) & 0xFFu) / 255.0,
        float((packed >> 16) & 0xFFu) / 255.0,
        float((packed >> 24) & 0xFFu) / 255.0
    );
}

#define MAX_USED_THREAD   16
#define BLOCK_IN_GROUP    4
#define THREAD_GROUP_SIZE 64
#define BLOCK_SIZE_Y      4
#define BLOCK_SIZE_X      4

groupshared float4 shared_temp[THREAD_GROUP_SIZE];

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main_cs(uint GI : SV_GroupIndex, uint3 groupID : SV_GroupID)
{
    uint num_block_x      = get_num_block_x();
    uint num_total_blocks = get_num_total_blocks();
    uint input_mip_offset = get_input_mip_offset();
    uint mip_width        = get_mip_width();
    uint mip_height       = get_mip_height();

    uint blockInGroup = GI / MAX_USED_THREAD;
    uint linear_group = groupID.y * get_groups_per_row() + groupID.x;
    uint blockID      = linear_group * BLOCK_IN_GROUP + blockInGroup;
    uint pixelBase    = blockInGroup * MAX_USED_THREAD;
    uint pixelInBlock = GI - pixelBase;

    bool valid_block = (blockID < num_total_blocks);

    uint block_y = blockID / num_block_x;
    uint block_x = blockID - block_y * num_block_x;
    uint base_x  = block_x * BLOCK_SIZE_X;
    uint base_y  = block_y * BLOCK_SIZE_Y;

    if (valid_block && pixelInBlock < 16)
    {
        uint px = min(base_x + pixelInBlock % 4, mip_width - 1);
        uint py = min(base_y + pixelInBlock / 4, mip_height - 1);
        shared_temp[GI] = unpack_rgba8(tex_compress_in[input_mip_offset + py * mip_width + px]);
    }

    GroupMemoryBarrierWithGroupSync();

    if (valid_block && pixelInBlock == 0)
    {
        float3 blockRGB[16];
        for (int i = 0; i < 16; i++)
        {
            blockRGB[i].x = shared_temp[pixelBase + i].x;
            blockRGB[i].y = shared_temp[pixelBase + i].y;
            blockRGB[i].z = shared_temp[pixelBase + i].z;
        }

        tex_compress_out_bc1[get_output_offset() + blockID] = CompressBlockBC1_UNORM(blockRGB, get_quality(), false);
    }
}
