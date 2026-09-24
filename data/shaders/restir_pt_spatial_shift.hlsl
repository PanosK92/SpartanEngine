/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================
#include "common.hlsl"
#include "restir_reservoir.hlsl"
//==============================

// paired spatial reuse pre-pass, lin 2026 3, every pixel shifts its own path to its paired
// partner in each pairing table and stores f_dst plus the jacobian, the resample pass then
// reads the forward shift from the partner and the backward shift from itself, so each pair
// pays one shift and one visibility ray per direction instead of two per neighbor
// a zero alpha marks a failed, incompatible or occluded shift

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    uint resolution_x, resolution_y;
    tex_uav.GetDimensions(resolution_x, resolution_y);
    float2 resolution = float2(resolution_x, resolution_y);

    if (pixel.x >= resolution_x || pixel.y >= resolution_y)
        return;

    float4 shift_out[RESTIR_PAIRING_COUNT];
    [unroll]
    for (uint i = 0; i < RESTIR_PAIRING_COUNT; i++)
    {
        shift_out[i] = float4(0, 0, 0, 0);
    }

    float2 uv   = (pixel + 0.5f) / resolution;
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;

    Reservoir center = unpack_reservoir(
        tex_reservoir_prev0[pixel],
        tex_reservoir_prev1[pixel],
        tex_reservoir_prev2[pixel],
        tex_reservoir_prev3[pixel],
        tex_reservoir_prev4[pixel],
        tex_reservoir_prev5[pixel]
    );

    bool has_sample = depth > 0.0f && is_reservoir_valid(center) && center.M > 0.0f && center.W > 0.0f;
    if (has_sample)
    {
        float  linear_depth = linearize_depth(depth);
        float3 pos_ws       = restir_primary_position(uv);
        float3 normal_ws    = get_normal(uv);

        for (uint t = 0; t < RESTIR_PAIRING_COUNT; t++)
        {
            int2 partner = restir_pairing_partner(pixel, t);

            if (!is_neighbor_gbuffer_compatible(partner, pos_ws, normal_ws, linear_depth, resolution))
                continue;

            float2 partner_uv        = (partner + 0.5f) / resolution;
            float3 partner_pos_ws    = restir_primary_position(partner_uv);
            float4 partner_material  = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), partner_uv, 0);
            float3 partner_albedo    = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), partner_uv, 0).rgb);
            float  partner_roughness = max(partner_material.r, 0.04f);
            float  partner_metallic  = partner_material.g;
            float3 partner_normal_ws = get_normal(partner_uv);
            float3 partner_view_dir  = normalize(get_camera_position() - partner_pos_ws);

            ShiftResult shift = try_reconnection_shift(
                center.sample,
                pos_ws,
                partner_pos_ws,
                partner_normal_ws,
                partner_view_dir,
                partner_albedo,
                partner_roughness,
                partner_metallic
            );

            if (!shift.ok || shift.jacobian <= 0.0f)
                continue;

            if (!trace_shift_visibility(shift.sample, partner_pos_ws, partner_normal_ws))
                continue;

            shift_out[t] = float4(shift.f_dst, shift.jacobian);
        }
    }

    tex_uav [pixel] = shift_out[0];
    tex_uav2[pixel] = shift_out[1];
    tex_uav3[pixel] = shift_out[2];
}
