/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
