#include "../../data/shaders/common.hlsl"
#include "../../data/shaders/grass_distribution.hlsl"
[numthreads(8, 8, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    uint index = id.y * 512u + id.x;
    uint count = (uint)buffer_pass.values[0].x;
    uint symmetry = (uint)buffer_pass.values[0].y;
    uint capacity = max(count, (uint)buffer_pass.values[0].z);
    float2 random = float2(hash(float2(index, 17)), hash(float2(index, 83)));
    float2 p = grass_stratified_position(index % count, capacity, random, symmetry);
    tex_uav[id.xy] = float4(p, 0, 0);
}
