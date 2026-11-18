// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
#include "road_wear.hlsl"

// Input and output are linear arrays of packed texels for one horizontal strip.
// The maps use the same tangent convention as the source OpenGL normal texture.
StructuredBuffer<uint2> source_maps : register(t0);
RWStructuredBuffer<uint4> baked_maps : register(u1);

float3 unpack_rgb(uint v)
{
    return float3(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u) / 255.0f;
}

uint pack_rgb(float3 v)
{
    uint3 c = (uint3)round(saturate(v) * 255.0f);
    return c.x | (c.y << 8) | (c.z << 16);
}

struct Finish { float3 albedo, normal; float roughness, ao, height; };

Finish evaluate(float lateral, float along, float3 color, float3 normal, float roughness, float height, float ao, bool paint_layer)
{
    const uint flags = (30u << 26) | (paint_layer ? 1u << 23 : 1u << 22);
    float2 uv = float2(lateral, along) / road_asphalt_repeat;
    float3 world = float3(112.3f + lateral, 0, 247.5f - along);
    const float3 up = float3(0,1,0), right = float3(1,0,0);
    RoadWear wear = road_wear(flags, world, uv, up, right, FOOTPRINT);
    Finish result;
    result.albedo = paint_layer ? color : color * road_asphalt_tint;
    result.normal = normalize(float3(normal.x, normal.z, -normal.y));
    result.roughness = roughness;
    result.ao = ao;
    float paint = road_weathering(flags, wear, world, uv, height, result.albedo, result.roughness, FOOTPRINT);
    road_surface_detail(flags, wear, world, uv, up, height, paint, result.albedo, result.normal, result.ao);
    result.height = saturate(road_macro_height(flags, wear, height) - wear.polish * .08f);
    return result;
}

Finish blend_finish(Finish a, Finish b, float t)
{
    a.albedo = lerp(a.albedo,b.albedo,t);
    a.normal = lerp(a.normal,b.normal,t);
    a.roughness = lerp(a.roughness,b.roughness,t);
    a.ao = lerp(a.ao,b.ao,t);
    a.height = lerp(a.height,b.height,t);
    return a;
}

// The atlas holds unmarked junction asphalt and 5, 8 and 15 metre road profiles.
Finish road_panel(float x, float z, float3 color, float3 normal, float rough, float height, float ao)
{
    if (PAINT) return evaluate(x,z,color,normal,rough,height,ao,true);
    float width = x < 8 ? 8 : x < 13 ? 5 : x < 21 ? 8 : 15;
    float left = x < 8 ? 0 : x < 13 ? 8 : x < 21 ? 13 : 21;
    float lateral = x - left - width * .5f;
    Finish base = evaluate(lateral,z,color,normal,rough,height,ao,false);
    if (left == 0) return base;

    float a = abs(lateral), edge = width * .5f - .5f;
    float distance = abs(a - edge);
    float half_width = .06f;
    if (width == 15) distance = min(distance, abs(a - .12f));
    // Three metres of paint in each nine metre dash cycle.
    float dash = frac(z / 9.0f) * 9.0f;
    float divider = width == 15 ? abs(a - edge * .5f) : a;
    if (width >= 8 && dash < 3 && divider < distance)
    {
        distance = divider;
        half_width = width == 8 ? .065f : .06f;
    }
    float coverage = 1 - smoothstep(half_width - FOOTPRINT, half_width + FOOTPRINT, distance);
    if (coverage > 0)
        base = blend_finish(base,evaluate(distance,z,color,normal,rough,height,ao,true),coverage);
    return base;
}

[numthreads(8,8,1)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint i = tid.x + tid.y * WIDTH;
    uint2 src = source_maps[i];
    float3 srgb = unpack_rgb(src.x);
    float3 color = pow((srgb + 0.055f) / 1.055f, 2.4f);
    color = float3(srgb.x <= .04045f ? srgb.x / 12.92f : color.x,
                   srgb.y <= .04045f ? srgb.y / 12.92f : color.y,
                   srgb.z <= .04045f ? srgb.z / 12.92f : color.z);
    float2 n = unpack_rgb(src.y).xy * 2 - 1;
    float3 normal = float3(n, sqrt(saturate(1-dot(n,n))));
    float rough = (src.x >> 24) / 255.0f, height = (src.y >> 24) / 255.0f, ao = ((src.y >> 16) & 255u) / 255.0f;
    uint y_offset = source_maps[WIDTH * STRIP_HEIGHT].x;
    float2 uv = (float2(tid.xy + uint2(0,y_offset)) + .5f) / float2(WIDTH,HEIGHT);
    float x = (uv.x - (PAINT ? .5f : 0.0f)) * METRES_X, z = uv.y * METRES_Y;
    Finish f = road_panel(x,z,color,normal,rough,height,ao);
    // Crossfade only at the wrap. The underlying stone texture has an integer
    // number of repeats down the strip and the road features meet continuously.
    float seam = smoothstep(.90f,1.0f,uv.y);
    if (seam > 0) f = blend_finish(f,road_panel(x,z-METRES_Y,color,normal,rough,height,ao),seam);
    float3 baked_color = max(1.055f * pow(saturate(f.albedo), 1.0f/2.4f) - .055f, 0);
    float3 baked_normal = normalize(float3(f.normal.x,-f.normal.z,f.normal.y));
    baked_maps[i] = uint4(pack_rgb(baked_color), pack_rgb(baked_normal * .5f + .5f),
        pack_rgb(float3(f.ao,f.roughness,f.height)), 0);
}
