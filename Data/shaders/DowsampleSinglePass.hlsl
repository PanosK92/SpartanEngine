/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==========
#include "Common.hlsl"
//=====================

//--------------------------------------------------------------------------------------
// Constant Buffer
//--------------------------------------------------------------------------------------
cbuffer spdConstants : register(b0)
{
    uint mips;
    uint numWorkGroups;
    // [SAMPLER]
    float2 invInputSize;
}

struct globalAtomicBuffer
{
    uint counter;
};
globallycoherent RWStructuredBuffer<globalAtomicBuffer> globalAtomic :register(u1);

groupshared uint spd_counter;
groupshared float spd_intermediateR[16][16];
groupshared float spd_intermediateG[16][16];
groupshared float spd_intermediateB[16][16];
groupshared float spd_intermediateA[16][16];

float4 SpdLoadSourceImage(int2 p)
{
    float2 textureCoord = p * invInputSize + invInputSize;
    return tex.SampleLevel(sampler_bilinear_clamp, textureCoord, 0);
}

float4 SpdLoad(int2 tex) { return tex_out_rgba_mips[5][tex]; }

void SpdStore(int2 pix, float4 outValue, uint index) { tex_out_rgba_mips[index][pix] = outValue; }

void SpdIncreaseAtomicCounter() { InterlockedAdd(globalAtomic[0].counter, 1, spd_counter); }

uint SpdGetAtomicCounter() { return spd_counter; }

float4 SpdLoadIntermediate(uint x, uint y)
{
    return float4(
    spd_intermediateR[x][y], 
    spd_intermediateG[x][y], 
    spd_intermediateB[x][y], 
    spd_intermediateA[x][y]);
}

void SpdStoreIntermediate(uint x, uint y, float4 value)
{
    spd_intermediateR[x][y] = value.x;
    spd_intermediateG[x][y] = value.y;
    spd_intermediateB[x][y] = value.z;
    spd_intermediateA[x][y] = value.w;
}

float4 SpdReduce4(float4 v0, float4 v1, float4 v2, float4 v3) { return (v0 + v1 + v2 + v3) * 0.25f; }

#define A_GPU
#define A_HLSL
#define SPD_NO_WAVE_OPERATIONS
#define SPD_LINEAR_SAMPLER

#include "ffx_a.h"
#include "ffx_spd.h"

[numthreads(256, 1, 1)]
void mainCS(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
    SpdDownsample(
        uint2(WorkGroupId.xy), 
        uint(LocalThreadIndex),  
        uint(mips),
        uint(numWorkGroups));
 }
