/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "common.hlsl"
#include "restir_reservoir.hlsl"

#if defined(ENV_MARGINAL)
groupshared float prefix[512];
[numthreads(512, 1, 1)]
void main_cs(uint lane : SV_GroupIndex)
{
    float mass = restir_environment_cdf[RESTIR_ENV_CELLS + lane].x;
    prefix[lane] = mass;
    GroupMemoryBarrierWithGroupSync();
    for (uint step = 1u; step < RESTIR_ENV_HEIGHT; step *= 2u)
    {
        float addend = lane >= step ? prefix[lane - step] : 0.0f;
        GroupMemoryBarrierWithGroupSync();
        prefix[lane] += addend;
        GroupMemoryBarrierWithGroupSync();
    }
    // Empty intervals must stay empty despite rounding in the parallel scan.
    prefix[lane] = mass > 0.0f ? prefix[lane] : 0.0f;
    GroupMemoryBarrierWithGroupSync();
    // Different summation orders can round adjacent prefixes out of order.
    // A prefix maximum restores a valid CDF even for high-dynamic-range skies.
    for (uint step = 1u; step < RESTIR_ENV_HEIGHT; step *= 2u)
    {
        float previous = lane >= step ? prefix[lane - step] : 0.0f;
        GroupMemoryBarrierWithGroupSync();
        prefix[lane] = max(prefix[lane], previous);
        GroupMemoryBarrierWithGroupSync();
    }
    restir_environment_cdf[RESTIR_ENV_CELLS + lane] = float2(mass, prefix[lane]);
}
#else
groupshared float prefix[1024];
[numthreads(1024, 1, 1)]
void main_cs(uint lane : SV_GroupIndex, uint3 group : SV_GroupID)
{
    uint row = group.x;
    float phi = ((float(lane) + 0.5f) / float(RESTIR_ENV_WIDTH) - 0.5f) * PI2;
    float y = cos(PI * (float(row) + 0.5f) / float(RESTIR_ENV_HEIGHT));
    float mass = target_scalar(sample_sky(restir_environment_direction(phi, y))) * restir_environment_cell_area(row);
    prefix[lane] = mass;
    GroupMemoryBarrierWithGroupSync();
    for (uint step = 1u; step < RESTIR_ENV_WIDTH; step *= 2u)
    {
        float addend = lane >= step ? prefix[lane - step] : 0.0f;
        GroupMemoryBarrierWithGroupSync();
        prefix[lane] += addend;
        GroupMemoryBarrierWithGroupSync();
    }
    // Empty intervals must stay empty despite rounding in the parallel scan.
    prefix[lane] = mass > 0.0f ? prefix[lane] : 0.0f;
    GroupMemoryBarrierWithGroupSync();
    // Different summation orders can round adjacent prefixes out of order.
    // A prefix maximum restores a valid CDF even for high-dynamic-range skies.
    for (uint step = 1u; step < RESTIR_ENV_WIDTH; step *= 2u)
    {
        float previous = lane >= step ? prefix[lane - step] : 0.0f;
        GroupMemoryBarrierWithGroupSync();
        prefix[lane] = max(prefix[lane], previous);
        GroupMemoryBarrierWithGroupSync();
    }
    restir_environment_cdf[row * RESTIR_ENV_WIDTH + lane] = float2(mass, prefix[lane]);
    if (lane == RESTIR_ENV_WIDTH - 1u)
        restir_environment_cdf[RESTIR_ENV_CELLS + row] = float2(prefix[lane], 0.0f);
}
#endif
