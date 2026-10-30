/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "common.hlsl"
#include "restir_reservoir.hlsl"

// Section 6.2.2: compact pixel-neighbor replay work before tracing it. The
// local queue bounds storage and avoids a global counter/indirect-dispatch sync.
// Every queued pair has one lane; reconnectable pairs never enter this loop.
groupshared uint replay_count;
groupshared uint3 replay_pairs[THREAD_GROUP_COUNT * RESTIR_PAIRING_COUNT];

void store_pair_shift(uint2 pixel, uint table, float4 value)
{
    if (table == 0u) tex_uav[pixel] = value;
    else if (table == 1u) tex_uav2[pixel] = value;
    else tex_uav3[pixel] = value;
}

Reservoir load_pair_reservoir(uint2 pixel)
{
    return unpack_reservoir(tex_reservoir_prev0[pixel], tex_reservoir_prev1[pixel],
        tex_reservoir_prev2[pixel], tex_reservoir_prev3[pixel],
        tex_reservoir_prev4[pixel], tex_reservoir_prev5[pixel]);
}

float4 evaluate_pair_shift(uint2 pixel, uint table, Reservoir center, float2 resolution)
{
    int2 partner = restir_pairing_partner(pixel, table);
    float2 uv = restir_primary_uv((pixel + 0.5f) / resolution);
    float3 position = restir_primary_position(uv);
    float2 partner_uv = restir_primary_uv((partner + 0.5f) / resolution);
    float3 partner_position = restir_primary_position(partner_uv);
    float4 material = get_material(partner_uv);
    float3 normal = get_normal(partner_uv);
    float3 geometric_normal = get_geometric_normal(partner_uv);
    ShiftResult shift = try_reconnection_shift(center.sample, position, partner_position, normal,
        normalize(get_camera_position() - partner_position),
        saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), partner_uv, 0).rgb),
        max(material.r, 0.04f), material.g, normalize(get_camera_position() - position), geometric_normal);
    if (!shift.ok || shift.jacobian <= 0.0f)
        return 0.0f;
    if (!trace_shift_visibility(shift.sample, partner_position, geometric_normal))
        return 0.0f;
    return float4(shift.f_dst, shift.jacobian);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
    if (group_index == 0u)
        replay_count = 0u;
    GroupMemoryBarrierWithGroupSync();

    uint2 pixel = dispatch_id.xy;
    uint2 size;
    tex_uav.GetDimensions(size.x, size.y);
    float2 resolution = float2(size);
    if (all(pixel < size))
    {
        tex_uav[pixel] = 0.0f;
        tex_uav2[pixel] = 0.0f;
        tex_uav3[pixel] = 0.0f;
        float2 uv = restir_primary_uv((pixel + 0.5f) / resolution);
        float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
        Reservoir center = load_pair_reservoir(pixel);
        if (depth > 0.0f && is_reservoir_valid(center) && center.M > 0.0f && center.W > 0.0f)
        {
            float3 position = restir_primary_position(uv);
            float3 normal = get_normal(uv);
            bool needs_replay = center.sample.rc_length > 2u ||
                (!has_reconnection(center.sample) && (!is_sky_sample(center.sample) || !is_nee_sample(center.sample)));
            for (uint t = 0u; t < RESTIR_PAIRING_COUNT; ++t)
            {
                int2 partner = restir_pairing_partner(pixel, t);
                if (!is_neighbor_gbuffer_compatible(partner, position, normal, linearize_depth(depth), resolution))
                    continue;
                if (needs_replay)
                {
                    uint slot;
                    InterlockedAdd(replay_count, 1u, slot);
                    replay_pairs[slot] = uint3(pixel, t);
                }
                else
                    store_pair_shift(pixel, t, evaluate_pair_shift(pixel, t, center, resolution));
            }
        }
    }
    // No early returns: even edge/sky lanes participate in replay compaction.
    // Order the UAV clears before another lane writes a queued pair's result.
    AllMemoryBarrierWithGroupSync();
    for (uint i = group_index; i < replay_count; i += THREAD_GROUP_COUNT)
    {
        uint3 pair = replay_pairs[i];
        store_pair_shift(pair.xy, pair.z,
            evaluate_pair_shift(pair.xy, pair.z, load_pair_reservoir(pair.xy), resolution));
    }
}
