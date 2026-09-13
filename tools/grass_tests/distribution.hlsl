#include "../../data/shaders/common.hlsl"
#include "../../data/shaders/grass_distribution.hlsl"
RWStructuredBuffer<uint> allocation_counts : register(u61);
#define grass_count allocation_counts
#include "../../data/shaders/grass_lod_allocation.hlsl"
#undef grass_count
[numthreads(8, 8, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    uint index = id.y * 512u + id.x;
    if (buffer_pass.values[1].x > 0.0f)
    {
        uint capacity = (uint)buffer_pass.values[0].x;
        uint ring = (uint)buffer_pass.values[0].y;
        uint mode = (uint)buffer_pass.values[0].z;
        bool coarse = mode == 1u || (mode == 2u && (index & 1u) != 0u);
        float4 result = 0;
        if (index < capacity * 2u + 17u && grass_reserve_instance(ring, capacity))
            result = float4(grass_allocate_detail_bin(ring, capacity, coarse) + 1u, coarse ? 1 : 0, 0, 0);
        tex_uav[id.xy] = result;
        return;
    }
    uint count = (uint)buffer_pass.values[0].x;
    uint symmetry = (uint)buffer_pass.values[0].y;
    uint capacity = max(count, (uint)buffer_pass.values[0].z);
    float2 random = float2(hash(float2(index, 17)), hash(float2(index, 83)));
    float2 p = grass_stratified_position(index % count, capacity, random, symmetry);
    tex_uav[id.xy] = float4(p, 0, 0);
}
