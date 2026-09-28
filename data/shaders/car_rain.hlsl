/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// the water on the occupied car lives on the cpu (CarRain.cpp), this keeps the gpu's copy of it current
// CLEAR  wipes the drop id of every atlas texel
// SPLAT  stamps every drop's id over the texels it covers, bigger drops win where two overlap
// TEXELS writes the micro water the simulation changed this frame
// values[0] x = element offset of this frame's slice, y = element count, z = atlas width
// values[1] x = atlas height, y = texel size in metres

#include "common.hlsl"

struct CarRainDropData
{
    float2 uv;     // atlas texel coordinates of the centre
    float  radius; // metres, 0 for a free slot
    float  seed;
    float3 lean;
    float  speed;
};

struct CarRainTexelData
{
    uint  texel;
    float mass;
    float stamp;
    float padding;
};

StructuredBuffer<CarRainDropData> car_rain_drops   : register(t69);
StructuredBuffer<CarRainTexelData> car_rain_texels : register(t75);
[[vk::image_format("rg32f")]] RWTexture2D<float2> tex_car_rain_micro_uav : register(u73);
[[vk::image_format("r32ui")]] RWTexture2D<uint> tex_car_rain_ids_uav     : register(u74);

#if defined(CLEAR)
[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 size = uint2(pass_uint(pass_car_rain::atlas_width), pass_uint(pass_car_rain::atlas_height));
    if (any(thread_id.xy >= size))
        return;

    tex_car_rain_ids_uav[thread_id.xy] = 0u;
}
#endif

#if defined(SPLAT)
[numthreads(64, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint count = pass_uint(pass_car_rain::count);
    if (thread_id.x >= count)
        return;

    CarRainDropData drop = car_rain_drops[pass_uint(pass_car_rain::offset) + thread_id.x];
    if (drop.radius <= 0.0f)
        return;

    // the key sorts by radius in micrometres, the index sits below it, 0 stays free for no drop
    uint key       = (min(uint(drop.radius * 1.0e6f), 32767u) << 17) | (thread_id.x + 1u);
    float reach    = drop.radius / pass_float(pass_car_rain::texel_size) + 0.75f;
    int2 size      = int2(pass_uint(pass_car_rain::atlas_width), pass_uint(pass_car_rain::atlas_height));
    int2 centre    = int2(floor(drop.uv));
    int extent     = min(4, int(ceil(reach)));
    for (int y = -extent; y <= extent; y++)
    {
        for (int x = -extent; x <= extent; x++)
        {
            int2 texel = centre + int2(x, y);
            if (any(texel < 0) || any(texel >= size))
                continue;
            if (length(float2(texel) + 0.5f - drop.uv) > reach)
                continue;

            uint previous;
            InterlockedMax(tex_car_rain_ids_uav[uint2(texel)], key, previous);
        }
    }
}
#endif

#if defined(TEXELS)
[numthreads(64, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint count = pass_uint(pass_car_rain::count);
    if (thread_id.x >= count)
        return;

    CarRainTexelData change = car_rain_texels[pass_uint(pass_car_rain::offset) + thread_id.x];
    uint width              = pass_uint(pass_car_rain::atlas_width);
    tex_car_rain_micro_uav[uint2(change.texel % width, change.texel / width)] = float2(change.mass, change.stamp);
}
#endif
