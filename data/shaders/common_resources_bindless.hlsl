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
#if defined(API_WEBGPU)
// core webgpu has no binding arrays, the backend resamples every material texture into one layer of an array
// and material_textures[i] reads layer i, the uv wraps per layer so samplers behave the same
Texture2DArray material_texture_layers : register(t15, space1);
struct MaterialTexture2D
{
    uint layer;

    float4 Sample(SamplerState s, float2 uv)                                  { return material_texture_layers.Sample(s, float3(uv, layer)); }
    float4 SampleLevel(SamplerState s, float2 uv, float mip)                  { return material_texture_layers.SampleLevel(s, float3(uv, layer), mip); }
    float4 SampleGrad(SamplerState s, float2 uv, float2 ddx_uv, float2 ddy_uv) { return material_texture_layers.SampleGrad(s, float3(uv, layer), ddx_uv, ddy_uv); }
    void GetDimensions(uint mip, out uint width, out uint height, out uint levels)
    {
        uint elements;
        material_texture_layers.GetDimensions(mip, width, height, elements, levels);
    }
};
struct MaterialTextureLayers
{
    uint unused; // wgsl has no empty structs

    MaterialTexture2D operator[](uint index)
    {
        MaterialTexture2D texture;
        texture.layer = index;
        return texture;
    }
};
static MaterialTextureLayers material_textures;
#else
typedef Texture2D MaterialTexture2D;
Texture2D material_textures[]                            : register(t15, space1);
#endif
StructuredBuffer<MaterialParameters> material_parameters : register(t16, space2);
StructuredBuffer<LightParameters> light_parameters       : register(t17, space3);
StructuredBuffer<aabb> aabbs                             : register(t18, space4);
SamplerComparisonState samplers_comparison[]             : register(s0,  space6);
SamplerState samplers[]                                  : register(s1,  space7);

// bindless draw data - per-draw transforms, material indices, etc.
StructuredBuffer<DrawData> draw_data                     : register(t19, space5);

#if defined(API_WEBGPU)
// std430 aligns a float3 to 16 bytes, which would stretch the 24 byte vertices to 32, so they are read as scalars
struct PulledVertexScalars
{
    float position_x;
    float position_y;
    float position_z;
    uint  uv;
    uint  normal;
    uint  tangent;
};
StructuredBuffer<PulledVertexScalars> geometry_vertices : register(t20, space8);
#else
StructuredBuffer<PulledVertex> geometry_vertices    : register(t20, space8);
#endif
StructuredBuffer<uint> geometry_indices             : register(t22, space9);
StructuredBuffer<PackedInstance> geometry_instances : register(t23, space10);

PulledVertex load_pulled_vertex(uint index)
{
#if defined(API_WEBGPU)
    PulledVertexScalars scalars = geometry_vertices[index];
    PulledVertex vertex;
    vertex.position = float3(scalars.position_x, scalars.position_y, scalars.position_z);
    vertex.uv       = scalars.uv;
    vertex.normal   = scalars.normal;
    vertex.tangent  = scalars.tangent;
    return vertex;
#else
    return geometry_vertices[index];
#endif
}

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
