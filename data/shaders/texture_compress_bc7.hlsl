/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// gpu bc7 texture compression for opaque color maps, mode 6 only
// mode 6 stores one rgba line per 4x4 block with 7 bit endpoints plus a p-bit and 4 bit indices,
// so dark near-neutral colors keep their hue where bc1's 565 endpoints split them into tinted blocks
// each 64-thread group compresses 4 bc blocks, input is a flat buffer of packed rgba8 pixels (all mips concatenated)

#include "common_resources_buffers.hlsl"
#include "common_resources_gpu_driven.hlsl"

uint get_num_block_x()      { return asuint(buffer_pass.values[0].x); }
uint get_num_total_blocks() { return asuint(buffer_pass.values[0].y); }
uint get_input_mip_offset() { return asuint(buffer_pass.values[0].w); }
uint get_output_offset()    { return asuint(buffer_pass.values[1].x); }
uint get_mip_width()        { return asuint(buffer_pass.values[1].y); }
uint get_mip_height()       { return asuint(buffer_pass.values[1].z); }
uint get_groups_per_row()   { return asuint(buffer_pass.values[1].w); }

static const uint bc7_weights[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

#define MAX_USED_THREAD   16
#define BLOCK_IN_GROUP    4
#define THREAD_GROUP_SIZE 64

groupshared float3 shared_pixels[THREAD_GROUP_SIZE];

float3 unpack_rgb8(uint packed)
{
    return float3(float(packed & 0xFFu), float((packed >> 8) & 0xFFu), float((packed >> 16) & 0xFFu));
}

uint3 quantize_endpoint(float3 value, uint p_bit)
{
    return uint3(clamp(round((value - float(p_bit)) * 0.5f), 0.0f, 127.0f));
}

float3 expand_endpoint(uint3 value7, uint p_bit)
{
    return float3((value7 << 1) | p_bit);
}

// picks the closest of the 16 decoded palette entries for every pixel, returns the total squared error
float assign_indices(float3 pixels[16], float3 e0, float3 e1, out uint indices[16])
{
    float3 palette[16];
    for (uint k = 0; k < 16; k++)
    {
        uint w     = bc7_weights[k];
        palette[k] = floor(((64.0f - w) * e0 + w * e1 + 32.0f) / 64.0f);
    }

    float error = 0.0f;
    for (uint i = 0; i < 16; i++)
    {
        float best_error = 1e30f;
        uint best_index  = 0;
        for (uint j = 0; j < 16; j++)
        {
            float3 d = pixels[i] - palette[j];
            float  e = dot(d, d);
            if (e < best_error)
            {
                best_error = e;
                best_index = j;
            }
        }
        indices[i] = best_index;
        error     += best_error;
    }

    return error;
}

void write_bits(inout uint words[4], inout uint position, uint value, uint count)
{
    uint word  = position >> 5;
    uint shift = position & 31;
    words[word] |= value << shift;
    if (shift + count > 32)
    {
        words[word + 1] |= value >> (32 - shift);
    }
    position += count;
}

uint4 compress_block_bc7_mode6(float3 pixels[16])
{
    // principal axis of the block via power iteration on the covariance
    float3 mean      = 0.0f;
    float3 color_min = 255.0f;
    float3 color_max = 0.0f;
    for (uint i = 0; i < 16; i++)
    {
        mean     += pixels[i];
        color_min = min(color_min, pixels[i]);
        color_max = max(color_max, pixels[i]);
    }
    mean /= 16.0f;

    float3 cov_diag = 0.0f;
    float3 cov_off  = 0.0f; // rg, rb, gb
    for (uint i = 0; i < 16; i++)
    {
        float3 d  = pixels[i] - mean;
        cov_diag += d * d;
        cov_off  += float3(d.x * d.y, d.x * d.z, d.y * d.z);
    }

    float3 axis = color_max - color_min;
    if (dot(axis, axis) < 1e-6f)
    {
        axis = float3(1.0f, 1.0f, 1.0f);
    }
    for (uint iteration = 0; iteration < 8; iteration++)
    {
        float3 next = float3(
            cov_diag.x * axis.x + cov_off.x * axis.y + cov_off.y * axis.z,
            cov_off.x  * axis.x + cov_diag.y * axis.y + cov_off.z * axis.z,
            cov_off.y  * axis.x + cov_off.z * axis.y + cov_diag.z * axis.z
        );
        float length_sq = dot(next, next);
        if (length_sq < 1e-12f)
        {
            break;
        }
        axis = next * rsqrt(length_sq);
    }
    axis = normalize(axis);

    float t_min = 1e30f;
    float t_max = -1e30f;
    for (uint i = 0; i < 16; i++)
    {
        float t = dot(pixels[i] - mean, axis);
        t_min   = min(t_min, t);
        t_max   = max(t_max, t);
    }
    float3 e0 = clamp(mean + axis * t_min, 0.0f, 255.0f);
    float3 e1 = clamp(mean + axis * t_max, 0.0f, 255.0f);

    // alternate between picking p-bits/indices and a least squares endpoint fit
    float best_error = 1e30f;
    uint3 best_q0    = 0;
    uint3 best_q1    = 0;
    uint  best_p0    = 0;
    uint  best_p1    = 0;
    uint  best_indices[16];
    for (uint i = 0; i < 16; i++)
    {
        best_indices[i] = 0;
    }

    for (uint pass = 0; pass < 3; pass++)
    {
        uint pass_indices[16];
        float pass_error = 1e30f;
        for (uint combo = 0; combo < 4; combo++)
        {
            uint  p0 = combo & 1;
            uint  p1 = combo >> 1;
            uint3 q0 = quantize_endpoint(e0, p0);
            uint3 q1 = quantize_endpoint(e1, p1);
            uint  indices[16];
            float error = assign_indices(pixels, expand_endpoint(q0, p0), expand_endpoint(q1, p1), indices);
            if (error < pass_error)
            {
                pass_error = error;
                pass_indices = indices;
            }
            if (error < best_error)
            {
                best_error   = error;
                best_q0      = q0;
                best_q1      = q1;
                best_p0      = p0;
                best_p1      = p1;
                best_indices = indices;
            }
        }

        if (best_error <= 0.0f)
        {
            break;
        }

        float  a = 0.0f;
        float  b = 0.0f;
        float  c = 0.0f;
        float3 x = 0.0f;
        float3 y = 0.0f;
        for (uint i = 0; i < 16; i++)
        {
            float w  = bc7_weights[pass_indices[i]] / 64.0f;
            float iw = 1.0f - w;
            a += iw * iw;
            b += iw * w;
            c += w * w;
            x += iw * pixels[i];
            y += w * pixels[i];
        }
        float det = a * c - b * b;
        if (abs(det) < 1e-6f)
        {
            break;
        }
        e0 = clamp((c * x - b * y) / det, 0.0f, 255.0f);
        e1 = clamp((a * y - b * x) / det, 0.0f, 255.0f);
    }

    // the anchor (pixel 0) index is stored with 3 bits, so its msb must be zero
    if (best_indices[0] >= 8)
    {
        uint3 q = best_q0;
        best_q0 = best_q1;
        best_q1 = q;
        uint p  = best_p0;
        best_p0 = best_p1;
        best_p1 = p;
        for (uint i = 0; i < 16; i++)
        {
            best_indices[i] = 15 - best_indices[i];
        }
    }

    uint words[4] = { 0, 0, 0, 0 };
    uint position = 0;
    write_bits(words, position, 1u << 6, 7); // mode 6
    write_bits(words, position, best_q0.x, 7);
    write_bits(words, position, best_q1.x, 7);
    write_bits(words, position, best_q0.y, 7);
    write_bits(words, position, best_q1.y, 7);
    write_bits(words, position, best_q0.z, 7);
    write_bits(words, position, best_q1.z, 7);
    write_bits(words, position, 127, 7); // opaque alpha, the c++ side routes textures with alpha to bc3
    write_bits(words, position, 127, 7);
    write_bits(words, position, best_p0, 1);
    write_bits(words, position, best_p1, 1);
    write_bits(words, position, best_indices[0], 3);
    for (uint i = 1; i < 16; i++)
    {
        write_bits(words, position, best_indices[i], 4);
    }

    return uint4(words[0], words[1], words[2], words[3]);
}

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main_cs(uint GI : SV_GroupIndex, uint3 groupID : SV_GroupID)
{
    uint num_block_x      = get_num_block_x();
    uint num_total_blocks = get_num_total_blocks();
    uint input_mip_offset = get_input_mip_offset();
    uint mip_width        = get_mip_width();
    uint mip_height       = get_mip_height();

    uint block_in_group = GI / MAX_USED_THREAD;
    uint linear_group   = groupID.y * get_groups_per_row() + groupID.x;
    uint block_id       = linear_group * BLOCK_IN_GROUP + block_in_group;
    uint pixel_base     = block_in_group * MAX_USED_THREAD;
    uint pixel_in_block = GI - pixel_base;

    // every thread must reach the barrier, so work is guarded instead of returning early
    bool valid_block = (block_id < num_total_blocks);

    uint block_y = block_id / num_block_x;
    uint block_x = block_id - block_y * num_block_x;

    if (valid_block)
    {
        uint px = min(block_x * 4 + pixel_in_block % 4, mip_width - 1);
        uint py = min(block_y * 4 + pixel_in_block / 4, mip_height - 1);
        shared_pixels[GI] = unpack_rgb8(tex_compress_in[input_mip_offset + py * mip_width + px]);
    }

    GroupMemoryBarrierWithGroupSync();

    if (valid_block && pixel_in_block == 0)
    {
        float3 pixels[16];
        for (uint i = 0; i < 16; i++)
        {
            pixels[i] = shared_pixels[pixel_base + i];
        }

        tex_compress_out[get_output_offset() + block_id] = compress_block_bc7_mode6(pixels);
    }
}
