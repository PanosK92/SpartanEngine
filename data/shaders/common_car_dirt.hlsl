/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// the dirt on the occupied car, deposited grain by grain on the cpu (CarDirt.cpp) into the texels of its rain atlas
// a texel is a few millimetres of paint holding a deposit, the pixel resolves it into grains finer than the texel, so the
// dirt reads as overspray: a few specks where little landed, a speckled haze, and a crust once enough has built up
// requires common_rain.hlsl, the atlas mapping is the one the car's water uses
#ifndef SPARTAN_CAR_DIRT
#define SPARTAN_CAR_DIRT

Texture2D<uint> tex_car_dirt_color : register(t90); // rgb sqrt encoded albedo and coverage, 8 bits each
Texture2D<uint> tex_car_dirt_state : register(t91); // wet until in 1/16 s of the dirt clock << 8 | coarseness

// the last stretch of drying, where the dark wet sheen fades into the pale crust
static const float car_dirt_dry_seconds = 50.0f;

bool car_dirt_active()
{
    return buffer_frame.car_dirt.y > 0.0f && buffer_frame.rain_car_atlas.w > 0.0f;
}

float4 car_dirt_unpack(uint packed)
{
    float4 c = float4(packed & 0xffu, (packed >> 8) & 0xffu, (packed >> 16) & 0xffu, packed >> 24) / 255.0f;
    c.rgb *= c.rgb;
    return c;
}

// one octave of grains, every cell holds at most one, density decides how many cells have one and how big they are
// slope is the tilt of the grain cap the pixel sits on, in cell units of the projection plane
float car_dirt_grains(float2 q, float cell, float density, uint salt, inout float2 slope)
{
    float2 g     = q / cell;
    int2 base    = int2(floor(g));
    float cover  = 0.0f;
    float chance = saturate(density * 1.6f);
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            int2 c   = base + int2(x, y);
            uint h   = rain_hash_u(uint2(c) + uint2(salt * 7919u, salt * 104729u));
            float4 r = float4(h & 0xffu, (h >> 8) & 0xffu, (h >> 16) & 0xffu, h >> 24) / 255.0f;
            if (r.w >= chance)
                continue;

            float2 centre = float2(c) + 0.5f + (r.xy - 0.5f) * 0.9f;
            float radius  = (0.26f + 0.44f * r.z) * (0.7f + 0.65f * density);
            float2 d      = (g - centre) / radius;
            float grain   = saturate((1.0f - dot(d, d)) * 3.0f);
            if (grain > cover)
            {
                cover = grain;
                slope = d * grain;
            }
        }
    }
    return cover;
}

// returns how much of the pixel the dirt hides, false-y (0) when the pixel is not paint the atlas knows
float car_dirt_apply(float3 position, float3 geometric_normal, float footprint, inout float3 albedo, inout float3 normal, inout float roughness, inout float metalness, inout float occlusion, inout float3 emission)
{
    // the same mapping as rain_car(), car local, the face the normal looks along
    float3x3 r = float3x3(buffer_frame.rain_vehicle_axis[0].xyz, buffer_frame.rain_vehicle_axis[1].xyz, buffer_frame.rain_vehicle_axis[2].xyz);
    float3 p   = mul(r, position - buffer_frame.rain_car_origin.xyz);
    float3 n   = mul(r, geometric_normal);
    float3 a   = abs(n);
    uint axis  = a.x > a.y ? (a.x > a.z ? 0u : 2u) : (a.y > a.z ? 1u : 2u);
    float nd   = axis == 0u ? n.x : (axis == 1u ? n.y : n.z);
    float pd   = axis == 0u ? p.x : (axis == 1u ? p.y : p.z);
    uint face  = axis * 2u + (nd < 0.0f ? 1u : 0u);

    float s     = buffer_frame.rain_car_box.w;
    float4 rect = buffer_frame.rain_car_faces[face];
    float2 q    = rain_plane(p, axis);
    float2 uv   = (q - rain_plane(buffer_frame.rain_car_box.xyz, axis)) / s + 1.0f;
    if (any(uv < 1.0f) || any(uv >= rect.zw - 1.0f))
        return 0.0f;

    float2 atlas = rect.xy + uv;
    int2 texel   = int2(floor(atlas));
    if (abs(pd - tex_car_rain_surface.Load(int3(texel, 0))) > 0.015f)
        return 0.0f;

    // the deposit, bilinear so the field follows the spray instead of the texel grid
    float2 f   = atlas - 0.5f;
    int2 t0    = int2(floor(f));
    float2 w   = f - floor(f);
    float4 d00 = car_dirt_unpack(tex_car_dirt_color.Load(int3(t0, 0)));
    float4 d10 = car_dirt_unpack(tex_car_dirt_color.Load(int3(t0 + int2(1, 0), 0)));
    float4 d01 = car_dirt_unpack(tex_car_dirt_color.Load(int3(t0 + int2(0, 1), 0)));
    float4 d11 = car_dirt_unpack(tex_car_dirt_color.Load(int3(t0 + int2(1, 1), 0)));
    float4 wt  = float4((1.0f - w.x) * (1.0f - w.y), w.x * (1.0f - w.y), (1.0f - w.x) * w.y, w.x * w.y);
    float mass = dot(wt, float4(d00.a, d10.a, d01.a, d11.a));
    if (mass < 0.004f)
        return 0.0f;

    float3 color = (d00.rgb * (wt.x * d00.a) + d10.rgb * (wt.y * d10.a) + d01.rgb * (wt.z * d01.a) + d11.rgb * (wt.w * d11.a)) / mass;
    uint state   = tex_car_dirt_state.Load(int3(texel, 0));
    float coarse = float(state & 0xffu) / 255.0f;
    float wet    = saturate((float(state >> 8) / 16.0f - buffer_frame.car_dirt.x) / car_dirt_dry_seconds);

    // grains, a fine powder octave and a coarser one of crumbs and droplets, both finer than a texel
    // powder scatters far more light than the paint it hides, a thin layer already greys the paint out
    float density   = 1.0f - exp(-mass * 2.6f);
    float cell_fine = lerp(0.0007f, 0.0013f, coarse);
    float cell_big  = lerp(0.0015f, 0.0036f, coarse);
    float2 slope_fine = 0.0f;
    float2 slope_big  = 0.0f;
    float fine = car_dirt_grains(q, cell_fine, density, 3u, slope_fine);
    float big  = car_dirt_grains(q, cell_big, density * lerp(0.35f, 0.85f, coarse), 11u, slope_big);
    float grains = 1.0f - (1.0f - fine) * (1.0f - big);

    // grains smaller than the pixel average out to the share of the paint they hide
    float expected  = saturate(density * 1.15f);
    float resolved  = saturate(cell_fine / max(footprint, 1e-6f) * 0.8f - 0.3f);
    float film      = smoothstep(0.6f, 1.0f, mass);
    float coverage  = max(lerp(expected, grains, resolved), film);
    // a thin powder is a haze, it takes a thick layer to hide the paint completely
    coverage       *= lerp(0.78f, 1.0f, saturate(coarse * 2.0f + mass));

    // dry crumbs stand proud of the paint, wet slurry levels into a smooth skin
    float resolved_big = saturate(cell_big / max(footprint, 1e-6f) * 0.8f - 0.3f);
    float2 relief      = (slope_big * resolved_big * coarse + slope_fine * resolved * 0.4f) * lerp(0.45f, 0.12f, wet);
    float3 tilt        = mul(rain_lift(relief, axis, n), r);
    normal             = normalize(normal - tilt * coverage);

    // every crumb its own shade, water fills the pores so wet dirt is darker and glossy until it dries pale and matte
    float variation      = rain_hash(int2(floor(q / cell_big)), 23u);
    float3 dry           = color * lerp(0.82f, 1.16f, variation);
    float3 dirt          = dry * lerp(1.0f, 0.42f, wet);
    float dirt_roughness = lerp(lerp(0.7f, 0.95f, coarse), 0.2f, wet);
    albedo     = lerp(albedo, dirt, coverage);
    roughness  = lerp(roughness, dirt_roughness, coverage);
    metalness  = lerp(metalness, 0.0f, coverage);
    occlusion *= 1.0f - 0.2f * coverage * coarse * (1.0f - wet);
    emission  *= 1.0f - coverage;
    return coverage;
}
#endif
