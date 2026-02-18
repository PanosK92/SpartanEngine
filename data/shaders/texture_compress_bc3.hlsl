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

// gpu bc3 texture compression using amd compressonator kernels
// each 64-thread group compresses 4 bc blocks (4x4 pixels each)
// input is a flat buffer of packed rgba8 pixels (all mips concatenated)

#ifndef ASPM_HLSL
#define ASPM_HLSL
#endif

#include "compressonator/bcn_common_kernel.h"
#include "common_resources.hlsl"

// compression parameters packed into the push constant values:
// values[0].x = num_block_x      (uint via asuint)
// values[0].y = num_total_blocks  (uint via asuint)
// values[0].z = quality           (float)
// values[0].w = input_mip_offset  (uint via asuint) - pixel offset into input buffer
// values[1].x = output_offset     (uint via asuint) - block offset into output buffer
// values[1].y = mip_width         (uint via asuint) - width of this mip in pixels
// values[1].z = mip_height        (uint via asuint) - height of this mip in pixels
uint  get_num_block_x()      { return asuint(buffer_pass.values[0].x); }
uint  get_num_total_blocks() { return asuint(buffer_pass.values[0].y); }
float get_quality()          { return buffer_pass.values[0].z; }
uint  get_input_mip_offset() { return asuint(buffer_pass.values[0].w); }
uint  get_output_offset()    { return asuint(buffer_pass.values[1].x); }
uint  get_mip_width()        { return asuint(buffer_pass.values[1].y); }
uint  get_mip_height()       { return asuint(buffer_pass.values[1].z); }

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
    uint blockID      = groupID.x * BLOCK_IN_GROUP + blockInGroup;
    uint pixelBase    = blockInGroup * MAX_USED_THREAD;
    uint pixelInBlock = GI - pixelBase;

    if (blockID >= num_total_blocks)
        return;

    uint block_y = blockID / num_block_x;
    uint block_x = blockID - block_y * num_block_x;
    uint base_x  = block_x * BLOCK_SIZE_X;
    uint base_y  = block_y * BLOCK_SIZE_Y;

    // load 4x4 pixel block from the flat input buffer, clamping to mip edges
    if (pixelInBlock < 16)
    {
        uint px = min(base_x + pixelInBlock % 4, mip_width - 1);
        uint py = min(base_y + pixelInBlock / 4, mip_height - 1);
        shared_temp[GI] = unpack_rgba8(tex_compress_in[input_mip_offset + py * mip_width + px]);
    }

    GroupMemoryBarrierWithGroupSync();

    // thread 0 of each block compresses and writes the result
    if (pixelInBlock == 0)
    {
        float3 blockRGB[16];
        float  blockA[16];
        for (int i = 0; i < 16; i++)
        {
            blockRGB[i].x = shared_temp[pixelBase + i].x;
            blockRGB[i].y = shared_temp[pixelBase + i].y;
            blockRGB[i].z = shared_temp[pixelBase + i].z;
            blockA[i]     = shared_temp[pixelBase + i].w;
        }

        tex_compress_out[get_output_offset() + blockID] = CompressBlockBC3_UNORM(blockRGB, blockA, get_quality(), false);
    }
}
