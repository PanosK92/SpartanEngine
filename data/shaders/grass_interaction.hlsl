// Copyright(c) 2015-2026 Panos Karabelas
// Persistent tire deformation, independent of the transient grass instance pool.
#include "common.hlsl"

struct GrassWheelContact
{
    float4 start_width;        // xyz = previous contact, w = tire half width
    float4 end_length;         // xyz = current contact, w = longitudinal influence radius
    float4 direction_pressure; // xy = world xz bend direction, z = normalized tire load
    float4 normal;
};
RWStructuredBuffer<GrassWheelContact> grass_wheel_contacts : register(u61);

[numthreads(8, 8, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    tex_uav.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;

    float2 origin = buffer_pass.values[0].xy;
    float cell = buffer_pass.values[0].z;
    float2 world = origin + (float2(id.xy) + 0.5f) * cell;
    float dt = buffer_pass.values[1].w;
    // Integer scrolling preserves the exact history: bilinear reprojection would diffuse tracks.
    int2 previous = int2(id.xy) + int2(round((origin - buffer_pass.values[1].xy) / cell));
    float4 state = 0.0f;
    if (buffer_pass.values[0].w > 0.5f && all(previous >= 0) && all(previous < int2(width, height)))
        state = tex.Load(int3(previous, 0));

    float distance = length(world - buffer_pass.values[2].xy);
    float recovery = smoothstep(buffer_pass.values[2].z, buffer_pass.values[2].z + 3.0f, distance);
    state *= exp(-dt * buffer_pass.values[2].w * recovery);
    // Fade completely before history scrolls out of this 32 m field, even at high speed.
    float boundary = 1.0f - smoothstep(13.5f, 15.5f, distance);
    if (state.z > boundary)
        state *= boundary / max(state.z, 0.0001f);

    uint count = (uint)buffer_pass.values[1].z;
    for (uint i = 0; i < count; ++i)
    {
        GrassWheelContact wheel = grass_wheel_contacts[i];
        float2 segment = wheel.end_length.xz - wheel.start_width.xz;
        float segment_length_sq = dot(segment, segment);
        float t = saturate(dot(world - wheel.start_width.xz, segment) / max(segment_length_sq, 0.000001f));
        float3 contact = lerp(wheel.start_width.xyz, wheel.end_length.xyz, t);
        float2 direction = wheel.direction_pressure.xy;
        direction *= rsqrt(max(dot(direction, direction), 0.000001f));
        float2 side = float2(-direction.y, direction.x);
        float2 delta = world - contact.xz;
        float along = dot(delta, direction) / wheel.end_length.w;
        float across = dot(delta, side) / (wheel.start_width.w + cell);
        float coverage = 1.0f - smoothstep(0.55f, 1.0f, length(float2(along, across)));
        if (coverage <= 0.0f)
            continue;

        float ground_y = contact.y - dot(wheel.normal.xz, delta) / max(wheel.normal.y, 0.2f);
        if (state.z > 0.0f && abs(state.w / state.z - ground_y) > 0.5f)
            state = 0.0f;
        // Sweeps fill the spaces between frames. Fast rolling must still fully press a blade
        // even though the physical tire only covers it for a fraction of a rendered frame.
        float sweep_response = 1.0f - exp(-sqrt(segment_length_sq) / max(wheel.end_length.w, 0.05f) * 3.0f);
        float response = max(1.0f - exp(-dt * 24.0f), sweep_response) * wheel.direction_pressure.z;
        float pressed = max(state.z, lerp(state.z, coverage, response));
        float2 previous_direction = state.z > 0.0001f ? state.xy / state.z : direction;
        // Interpolate the angle, so opposite tire passes rotate the flattened blade smoothly
        // instead of cancelling its direction or making it stand back up.
        float turn = atan2(previous_direction.x * direction.y - previous_direction.y * direction.x,
                           dot(previous_direction, direction));
        float heading = atan2(previous_direction.y, previous_direction.x) + turn * coverage * response;
        float2 bent_direction = float2(cos(heading), sin(heading));
        state = float4(bent_direction * pressed, pressed, ground_y * pressed);
    }
    if (state.z < 0.0001f)
        state = 0.0f;
    tex_uav[id.xy] = state;
}
