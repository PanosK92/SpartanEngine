// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
// Transient vegetation deflection. Every cell is a damped spring: interactors push it out of
// their footprint and hand it their velocity, released cells swing back and settle.
#include "common.hlsl"

struct VegetationInteractor
{
    float4 center_radius; // xyz = centre, w = rounding radius
    float4 right;         // xyz = horizontal unit axis, w = half extent
    float4 forward;       // xyz = horizontal unit axis, w = half extent
    float4 velocity;      // xyz = world velocity
};
RWStructuredBuffer<VegetationInteractor> vegetation_interactors : register(u57);

static const float contact_margin   = 0.06f;
static const float displacement_max = 1.6f;
static const float velocity_max     = 7.0f;

[numthreads(8, 8, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    tex_uav.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;

    float2 origin = pass_float2(pass_vegetation_interaction::origin);
    float cell    = pass_float(pass_vegetation_interaction::cell_size);
    float2 world  = origin + (float2(id.xy) + 0.5f) * cell;
    float dt      = pass_float(pass_vegetation_interaction::delta_time);

    // integer scrolling keeps the history exact, bilinear reprojection would smear it
    int2 previous = int2(id.xy) + int2(round((origin - pass_float2(pass_vegetation_interaction::origin_previous)) / cell));
    float4 state  = 0.0f;
    if (pass_bool(pass_vegetation_interaction::history_valid) && all(previous >= 0) && all(previous < int2(width, height)))
        state = tex.Load(int3(previous, 0));
    float2 displacement = state.xy;
    float2 velocity     = state.zw;

    // semi implicit sub steps keep the spring stable on long frames
    float omega = pass_float(pass_vegetation_interaction::frequency) * PI2;
    float zeta  = pass_float(pass_vegetation_interaction::damping);
    uint steps  = clamp(uint(ceil(dt * omega / 0.5f)), 1u, 8u);
    float h     = dt / float(steps);
    for (uint s = 0; s < steps; ++s)
    {
        float2 acceleration = -omega * omega * displacement - 2.0f * zeta * omega * velocity;
        velocity     += acceleration * h;
        displacement += velocity * h;
    }

    uint count = pass_uint(pass_vegetation_interaction::interactor_count);
    for (uint i = 0; i < count; ++i)
    {
        VegetationInteractor interactor = vegetation_interactors[i];
        float2 axis_x = interactor.right.xz;
        float2 axis_z = interactor.forward.xz;
        float2 delta  = world - interactor.center_radius.xz;
        float2 local  = float2(dot(delta, axis_x), dot(delta, axis_z));
        float2 extent = float2(interactor.right.w, interactor.forward.w);
        float radius  = interactor.center_radius.w;

        // rounded rectangle footprint, signed distance and outward direction
        float2 q        = abs(local) - extent;
        float outside   = length(max(q, 0.0f));
        float distance  = outside + min(max(q.x, q.y), 0.0f) - radius;
        if (distance >= contact_margin)
            continue;

        float2 normal_local;
        if (outside > 1e-4f)
            normal_local = max(q, 0.0f) / outside * sign(local);
        else
            normal_local = q.x > q.y ? float2(sign(local.x), 0.0f) : float2(0.0f, sign(local.y));
        float2 normal = normalize(axis_x * normal_local.x + axis_z * normal_local.y + 1e-6f);

        // held at the surface of the shape, moving with it
        float required = contact_margin - distance;
        float along    = dot(displacement, normal);
        if (along < required)
            displacement += normal * (required - along);

        float2 body_velocity = interactor.velocity.xz;
        float normal_speed   = dot(body_velocity, normal);
        float cell_speed     = dot(velocity, normal);
        if (cell_speed < normal_speed)
            velocity += normal * (normal_speed - cell_speed);

        // brushing along the side drags the foliage a little in the direction of travel
        float2 tangent_body = body_velocity - normal * normal_speed;
        float2 tangent_cell = velocity - normal * dot(velocity, normal);
        velocity += (tangent_body * 0.35f - tangent_cell) * saturate(dt * 10.0f);
    }

    float displacement_length = length(displacement);
    if (displacement_length > displacement_max)
        displacement *= displacement_max / displacement_length;
    float velocity_length = length(velocity);
    if (velocity_length > velocity_max)
        velocity *= velocity_max / velocity_length;

    // fade towards the edge so history never scrolls out at full strength
    float field_radius = float(min(width, height)) * cell * 0.5f;
    float2 center      = origin + field_radius;
    float boundary     = 1.0f - smoothstep(field_radius - 6.0f, field_radius - 1.0f, length(world - center));
    displacement *= boundary;
    velocity     *= boundary;

    state = float4(displacement, velocity);
    if (dot(state, state) < 1e-10f)
        state = 0.0f;
    tex_uav[id.xy] = state;
}
