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

//= INCLUDES =========
#include "Common.hlsl"
//====================

struct globalAtomicBuffer { uint counter; };
globallycoherent RWStructuredBuffer<globalAtomicBuffer> globalAtomic : register(u4);

// 0 index in an array ?
Texture2D<float4> imgSrc : register(t0);
// Rest of the array
RWTexture2D<float4> imgDst[12] : register(u2);
// So basically, 13 mips ?

#define A_GPU 1
#define A_HLSL 1
#define SPD_LINEAR_SAMPLER 1
#define SPD_NO_WAVE_OPERATIONS 0
#include "ffx_a.h"

// Store current global atomic counter for all threads within the thread group
groupshared uint spd_counter; 

// Intermediate data storage for inter-mip exchange 
groupshared float spd_intermediateR[16][16];
groupshared float spd_intermediateG[16][16];
groupshared float spd_intermediateB[16][16];
groupshared float spd_intermediateA[16][16];

void SpdIncreaseAtomicCounter()
{
    InterlockedAdd(globalAtomic[0].counter, 1, spd_counter);
}

uint SpdGetAtomicCounter()
{
    return spd_counter;
}

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

float4 SpdReduce4(float4 v0, float4 v1, float4 v2, float4 v3)
{
    return (v0 + v1 + v2 + v3) * 0.25;
}

float4 SpdLoadSourceImage(int2 p)
{
    AF2 textureCoord = p * g_texel_size + g_texel_size;
    return imgSrc.SampleLevel(sampler_bilinear_clamp, textureCoord, 0);
}

float4 SpdLoad(int2 tex)
{
    return imgDst[5][tex];
}

void SpdStore(int2 pix, float4 outValue, uint index)
{
    imgDst[index][pix] = outValue;
}

#include "ffx_spd.h"

static const uint mips = 12;
static const uint numWorkGroups = 256; // x * y * z

[numthreads(256, 1, 1)]
void main(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
    SpdDownsample(
        AU2(WorkGroupId.xy),
        AU1(LocalThreadIndex),
        AU1(mips),
        AU1(numWorkGroups));
}
