// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
// Persistent tire deformation, independent of the transient grass instance pool.
#include "common.hlsl"

struct GrassWheelContact
{
    float4 start_width;        // xyz = previous contact (or foot position), w = half width
    float4 end_length;         // xyz = current contact, w = longitudinal influence radius
    float4 direction_pressure; // xy = world xz bend direction, z = normalized load, w = foot side (+1 right, -1 left, 0 = tire)
    float4 normal;             // xyz = ground normal, w = foot push (0 walk, 1 run)
};
RWStructuredBuffer<GrassWheelContact> grass_wheel_contacts : register(u61);

[numthreads(8, 8, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    tex_uav.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }

    float2 origin = pass_float2(pass_grass_interaction::origin);
    float cell = pass_float(pass_grass_interaction::cell_size);
    float2 world = origin + (float2(id.xy) + 0.5f) * cell;
    float dt = pass_float(pass_grass_interaction::delta_time);
    // Integer scrolling preserves the exact history: bilinear reprojection would diffuse tracks.
    int2 previous = int2(id.xy) + int2(round((origin - pass_float2(pass_grass_interaction::origin_previous)) / cell));
    float4 state = 0.0f;
    if (pass_bool(pass_grass_interaction::history_valid) && all(previous >= 0) && all(previous < int2(width, height)))
    {
        state = tex.Load(int3(previous, 0));
    }

    float distance = length(world - pass_float2(pass_grass_interaction::track_center));
    float track_radius = pass_float(pass_grass_interaction::track_radius);
    float recovery = smoothstep(track_radius, track_radius + 3.0f, distance);
    state *= exp(-dt * pass_float(pass_grass_interaction::track_recovery) * recovery);
    // Derive the fade from the field extent so larger fields preserve distant tracks.
    // Leave a margin for the snapped origin and filtering before history scrolls out.
    float field_radius = float(min(width, height)) * cell * 0.5f;
    float boundary = 1.0f - smoothstep(field_radius - 10.0f, field_radius - 2.0f, distance);
    if (state.z > boundary)
    {
        state *= boundary / max(state.z, 0.0001f);
    }

    uint count = pass_uint(pass_grass_interaction::contact_count);
    for (uint i = 0; i < count; ++i)
    {
        GrassWheelContact wheel = grass_wheel_contacts[i];
        float foot_side = wheel.direction_pressure.w;
        bool is_foot = abs(foot_side) > 0.5f;
        float2 direction = wheel.direction_pressure.xy;
        direction *= rsqrt(max(dot(direction, direction), 0.000001f));

        float3 contact;
        float2 delta;
        float coverage = 0.0f;
        float response = 0.0f;
        float2 bend_direction = direction;

        if (is_foot)
        {
            contact = wheel.end_length.xyz;
            delta = world - contact.xz;

            // subtle natural outward toe-splay (~7 deg) so alternating prints read unmistakably as human feet
            float2 right_dir = float2(direction.y, -direction.x);
            float splay = 0.12f * foot_side;
            float cs = cos(splay);
            float sn = sin(splay);
            float2 foot_fwd = cs * direction + sn * right_dir;
            float2 foot_right = -sn * direction + cs * right_dir;

            float u = dot(delta, foot_fwd);
            float v = dot(delta, foot_right);
            if (abs(u) > 0.34f || abs(v) > 0.22f)
            {
                continue;
            }

            float foot_push = saturate(wheel.normal.w);
            float aa = cell * 0.35f;

            // three-lobe anatomical sole: rounded heel, outer arch bridge (carving the inner medial arch),
            // and a slightly inward-set forefoot/ball pad matching TerrainDeformation::StampFoot
            float r_heel = length(float2((u + 0.120f) / (0.088f + aa), (v - 0.005f * foot_side) / (0.068f + aa)));
            float r_arch = length(float2((u + 0.015f) / (0.075f + aa), (v - 0.022f * foot_side) / (0.050f + aa)));
            float r_ball = length(float2((u - 0.080f) / (0.125f + aa), (v + 0.015f * foot_side) / (0.084f + aa)));
            float r_sole = min(r_heel, min(r_arch, r_ball));

            // firm full-core crush inside the sole so bilinear sampling keeps the impression deeply flattened
            float cov_heel = (1.0f - smoothstep(0.76f, 1.02f, r_heel)) * lerp(1.0f, 0.78f, foot_push);
            float cov_arch = (1.0f - smoothstep(0.70f, 1.00f, r_arch)) * 0.88f;
            float cov_ball = (1.0f - smoothstep(0.76f, 1.02f, r_ball)) * lerp(0.92f, 1.0f, foot_push);
            float sole_cov = max(cov_heel, max(cov_arch, cov_ball));

            // narrow outward canopy-parting rim around the sole: tall adjacent blades lean away from the shoe
            // instead of roofing over the footprint, keeping each individual shoe print readable in knee-high grass
            float rim_cov = (1.0f - smoothstep(0.95f, 1.48f, r_sole)) * 0.56f;
            coverage = max(sole_cov, rim_cov);
            if (coverage <= 0.0f)
            {
                continue;
            }

            // outward vector from the sole spine for parting blades cleanly away from the print perimeter
            float spine_u = clamp(u, -0.115f, 0.085f);
            float spine_v = lerp(0.005f, -0.015f, smoothstep(-0.06f, 0.06f, spine_u)) * foot_side;
            float2 outward_local = float2(u - spine_u, v - spine_v);
            float outward_len = length(outward_local);
            outward_local = outward_len > 0.001f ? outward_local / outward_len : float2(1.0f, 0.0f);

            // inside the sole, blades fold forward with slight outward splay; on the rim, they part outward
            float forward_bias = lerp(0.40f, 0.92f, smoothstep(-0.15f, 0.05f, u)) + 0.25f * foot_push;
            float2 core_local = float2(forward_bias, v * 5.0f);
            core_local *= rsqrt(max(dot(core_local, core_local), 0.000001f));
            float rim_blend = smoothstep(0.75f, 1.08f, r_sole);
            float2 local_bend = lerp(core_local, outward_local, rim_blend);
            local_bend *= rsqrt(max(dot(local_bend, local_bend), 0.000001f));
            bend_direction = foot_fwd * local_bend.x + foot_right * local_bend.y;

            // a footfall is stamped once per stride with the full stance load, not accumulated across frames
            response = wheel.direction_pressure.z;
        }
        else
        {
            float2 segment = wheel.end_length.xz - wheel.start_width.xz;
            float segment_length_sq = dot(segment, segment);
            float t = saturate(dot(world - wheel.start_width.xz, segment) / max(segment_length_sq, 0.000001f));
            contact = lerp(wheel.start_width.xyz, wheel.end_length.xyz, t);
            float2 side = float2(-direction.y, direction.x);
            delta = world - contact.xz;
            float along = dot(delta, direction) / wheel.end_length.w;
            float across = dot(delta, side) / (wheel.start_width.w + cell);
            coverage = 1.0f - smoothstep(0.55f, 1.0f, length(float2(along, across)));
            if (coverage <= 0.0f)
            {
                continue;
            }

            // Sweeps fill the spaces between frames. Fast rolling must still fully press a blade
            // even though the physical tire only covers it for a fraction of a rendered frame.
            float sweep_response = 1.0f - exp(-sqrt(segment_length_sq) / max(wheel.end_length.w, 0.05f) * 3.0f);
            response = max(1.0f - exp(-dt * 24.0f), sweep_response) * wheel.direction_pressure.z;
        }

        float ground_y = contact.y - dot(wheel.normal.xz, delta) / max(wheel.normal.y, 0.2f);
        if (state.z > 0.0f && abs(state.w / state.z - ground_y) > 0.5f)
        {
            state = 0.0f;
        }
        float pressed = max(state.z, lerp(state.z, coverage, response));
        float2 previous_direction = state.z > 0.0001f ? state.xy / state.z : bend_direction;
        // Interpolate the angle, so opposite passes rotate the flattened blade smoothly
        // instead of cancelling its direction or making it stand back up.
        float turn = atan2(previous_direction.x * bend_direction.y - previous_direction.y * bend_direction.x,
                           dot(previous_direction, bend_direction));
        float heading = atan2(previous_direction.y, previous_direction.x) + turn * coverage * response;
        float2 bent_direction = float2(cos(heading), sin(heading));
        state = float4(bent_direction * pressed, pressed, ground_y * pressed);
    }
    if (state.z < 0.0001f)
    {
        state = 0.0f;
    }
    tex_uav[id.xy] = state;
}
