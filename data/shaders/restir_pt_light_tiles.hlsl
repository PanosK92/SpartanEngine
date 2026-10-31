/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "common.hlsl"
#include "restir_reservoir.hlsl"

// Section 6.1: one stratified tile contains all available light kinds.
// Each source PDF includes the kind's actual fraction of the finite tile.
[numthreads(64, 1, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint index = dispatch_id.x;
    uint tile = index / RESTIR_LIGHT_TILE_SIZE;
    uint sample_index = index % RESTIR_LIGHT_TILE_SIZE;
    uint3 counts = restir_light_kind_counts();
    uint kind = sample_index < counts.x ? RESTIR_LIGHT_KIND_ENVIRONMENT :
        (sample_index < counts.x + counts.y ? RESTIR_LIGHT_KIND_EMISSIVE : RESTIR_LIGHT_KIND_ANALYTIC);
    uint local_index = sample_index - (kind == RESTIR_LIGHT_KIND_ENVIRONMENT ? 0u :
        (kind == RESTIR_LIGHT_KIND_EMISSIVE ? counts.x : counts.x + counts.y));
    uint seed = create_seed_for_pass(uint2(tile, 0u), buffer_frame.frame, 6u);
    float xi = frac(random_float(seed) + float(local_index) / float(counts[kind]));
    uint point_seed = create_seed_for_pass(uint2(index, 1u), buffer_frame.frame, 6u);
    RestirLightTileEntry cached = (RestirLightTileEntry)0;
    cached.lights = uint4(0xffffffffu, 0u, 0xffffffffu, 0u);
    float kind_pmf = restir_light_kind_pmf(kind);
    if (kind == RESTIR_LIGHT_KIND_ENVIRONMENT)
    {
        cached.environment_direction = sample_restir_environment(
            float2(xi, random_float(point_seed)), cached.environment_pdf);
        cached.environment_pdf *= kind_pmf;
    }
    else if (kind == RESTIR_LIGHT_KIND_EMISSIVE)
    {
        uint count = (uint)buffer_frame.restir_pt_emissive_tri_count;
        float total = emissive_triangles[count - 1u].cdf;
        if (total > 0.0f)
        {
            uint i = emtri_pick_index(min(xi, 0.99999994f) * total, count);
            float2 point_xi = random_float2(point_seed);
            float root = sqrt(point_xi.x);
            float2 bary = float2(root * (1.0f - point_xi.y), root * point_xi.y);
            cached.lights.yzw = uint3(asuint(kind_pmf * emissive_triangles[i].weight / total), i, pack_hit_barycentrics(bary));
        }
    }
    else
    {
        float total = compute_total_light_weight();
        float target = min(xi, 0.99999994f) * total;
        float cdf = 0.0f;
        for (uint i = 0u; i < (uint)buffer_frame.restir_pt_light_count; ++i)
        {
            float weight = light_pick_weight(light_parameters[i]);
            cdf += weight;
            if (weight > 0.0f && target < cdf)
            {
                cached.lights.xy = uint2(i, asuint(kind_pmf * weight / total));
                break;
            }
        }
        uint2 position = min(uint2(random_float2(point_seed) * 65536.0f), 65535u);
        cached.analytic_position = position.x | (position.y << 16u);
    }
    restir_light_tiles[index] = cached;
}
