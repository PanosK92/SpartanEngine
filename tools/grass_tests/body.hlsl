// Exercises the actual production deformation, not a reimplementation of it.
#include "../../data/shaders/common.hlsl"
#include "../../data/shaders/grass_body.hlsl"

[numthreads(8, 8, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    GrassBody body = grass_body_load(0);
    uint mode = (uint)buffer_pass.values[0].x;
    float h = id.x / 511.0;
    float row = id.y / 511.0;
    float3 root = float3(1.08 + row * 1.3, -0.9, 0.3);
    float3 offset = float3(-0.55 * h * h, h * 1.2, 0.015 * sin(row * 100.0) * h);
    if (mode == 1) root.x = lerp(-0.95, 0.95, row);
    if (mode == 2) { root.x = lerp(-0.45, 0.45, row); offset.y = h * 0.05; }
    if (mode == 3) root.x += 5.0;
    if (mode == 4) root.y = 1.0;
    if (mode == 5) { root.z = root.x + 1.0; root.x = 0.3; offset.z = offset.x; offset.x = 0.0; }
    // These blades lean behind an infinite side plane but never touch the bumper.
    if (mode == 6) root.z = 2.3;
    // Empty corner of a fitted hull inside its enclosing rectangular box.
    if (mode == 7)
    {
        root = float3(0.95 + row * 0.1, -0.9, 1.8);
        offset = float3(0.05 * h * h, h * 1.2, 0);
    }
    float3 world_root = body.center.xyz + grass_body_world(root, body);
    float3 position = world_root + grass_body_world(offset, body);
    float3 normal = body.forward.xyz;
    float3 tangent = body.right.xyz;
    bend_grass_around_body(body, world_root, 1.5, position, normal, tangent);
    tex_uav[id.xy] = float4(grass_body_local(position - body.center.xyz, body), dot(normal, normal) + dot(tangent, tangent));
}
