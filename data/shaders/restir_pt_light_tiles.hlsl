/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "common.hlsl"
#include "restir_reservoir.hlsl"

// Section 6.1: cache power-proportional draws once per frame.
// Use source PMFs, not the empirical frequency within the finite tile.
[numthreads(64, 1, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint index = dispatch_id.x;
    // Match GenerateLightTiles::getStratifiedRandom: one random rotation per
    // tile, with evenly spaced CDF samples. Each entry retains its original
    // marginal PMF; stratification must not replace it with a tile frequency.
    uint tile = index / RESTIR_LIGHT_TILE_SIZE;
    uint sample_index = index % RESTIR_LIGHT_TILE_SIZE;
    uint seed = create_seed_for_pass(uint2(tile, 0u), buffer_frame.frame, 6u);
    float offset = random_float(seed);
    float xi = frac(offset + float(sample_index) / float(RESTIR_LIGHT_TILE_SIZE));
    uint4 entry = uint4(0xffffffffu, 0u, 0xffffffffu, 0u);
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    float total = compute_total_light_weight();
    if (total > 0.0f)
    {
        float target = min(xi, 0.99999994f) * total;
        float cdf = 0.0f;
        for (uint i = 0u; i < light_count; ++i)
        {
            float weight = light_pick_weight(light_parameters[i]);
            cdf += weight;
            if (weight > 0.0f && target < cdf)
            {
                entry.xy = uint2(i, asuint(weight / total));
                break;
            }
        }
    }
    uint count = (uint)buffer_frame.restir_pt_emissive_tri_count;
    if (count > 0u)
    {
        float total_emissive = emissive_triangles[count - 1u].cdf;
        if (total_emissive > 0.0f)
        {
            uint i = emtri_pick_index(min(xi, 0.99999994f) * total_emissive, count);
            // A tile stores a complete, receiver-independent emitter sample.
            // The triangle PMF is recovered from its unchanged power/CDF data.
            uint point_seed = create_seed_for_pass(uint2(index, 1u), buffer_frame.frame, 6u);
            float2 point_xi = random_float2(point_seed);
            float root = sqrt(point_xi.x);
            float2 bary = float2(root * (1.0f - point_xi.y), root * point_xi.y);
            entry.zw = uint2(i, pack_hit_barycentrics(bary));
        }
    }
    restir_light_tiles[index] = entry;
}
