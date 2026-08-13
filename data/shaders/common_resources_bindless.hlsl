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

#ifndef SPARTAN_COMMON_RESOURCES_BINDLESS
#define SPARTAN_COMMON_RESOURCES_BINDLESS

#include "shared_buffers.h"

// bindless arrays
Texture2D material_textures[]                            : register(t15, space1);
StructuredBuffer<MaterialParameters> material_parameters : register(t16, space2);
StructuredBuffer<LightParameters> light_parameters       : register(t17, space3);
StructuredBuffer<aabb> aabbs                             : register(t18, space4);
SamplerComparisonState samplers_comparison[]             : register(s0,  space6);
SamplerState samplers[]                                  : register(s1,  space7);

// bindless draw data - per-draw transforms, material indices, etc.
StructuredBuffer<DrawData> draw_data                     : register(t19, space5);

StructuredBuffer<PulledVertex> geometry_vertices    : register(t20, space8);
StructuredBuffer<uint> geometry_indices             : register(t22, space9);
StructuredBuffer<PackedInstance> geometry_instances : register(t23, space10);

// vertex attribute unpackers, must match the cpu-side encoders in rhi_vertex.h
float2 unpack_vertex_uv(uint packed)
{
    return f16tof32(uint2(packed & 0xFFFFu, packed >> 16));
}

float3 unpack_vertex_oct(uint packed)
{
    // sign-extend snorm16 lanes into [-1, 1]
    int sx   = (int)(packed << 16) >> 16;
    int sy   = (int)packed >> 16;
    float2 f = max(float2(sx, sy) * (1.0f / 32767.0f), -1.0f);

    // branchless octahedral wrap (rune stubbe form)
    float3 n = float3(f, 1.0f - abs(f.x) - abs(f.y));
    float t  = max(-n.z, 0.0f);
    n.x     += n.x >= 0.0f ? -t : t;
    n.y     += n.y >= 0.0f ? -t : t;
    return normalize(n);
}

#endif
