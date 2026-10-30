/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "common.hlsl"
#include "restir_reservoir.hlsl"

// Section 6.1: cache independent power-proportional draws once per frame.
// Store source PMFs, not the empirical frequency within the finite tile.
[numthreads(64, 1, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint index = dispatch_id.x;
    uint seed = create_seed_for_pass(uint2(index, 0u), buffer_frame.frame, 6u);
    float2 xi = random_float2(seed);
    uint4 entry = uint4(0xffffffffu, 0u, 0xffffffffu, 0u);
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    float total = compute_total_light_weight();
    if (total > 0.0f)
    {
        float target = min(xi.x, 0.99999994f) * total;
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
            uint i = emtri_pick_index(min(xi.y, 0.99999994f) * total_emissive, count);
            entry.zw = uint2(i, asuint(emissive_triangles[i].weight / total_emissive));
        }
    }
    restir_light_tiles[index] = entry;
}
