/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// the dirt and the paint damage on the occupied car live on the cpu (CarDirt.cpp), this writes the texels it changed this frame into the gpu's copy
// values[0] x = element offset of this frame's slice, y = element count, z = atlas width

#include "common.hlsl"

struct CarDirtTexelData
{
    uint texel;
    uint color; // rgb sqrt encoded albedo and coverage, 8 bits each
    uint state; // wet until in 1/16 s of the dirt clock << 8 | coarseness
    uint wear;  // paint cut depth | groove direction << 8 | strands << 16 | rust << 24
};

StructuredBuffer<CarDirtTexelData> car_dirt_texels                       : register(t92);
[[vk::image_format("r32ui")]] RWTexture2D<uint> tex_car_dirt_color_uav : register(u90);
[[vk::image_format("r32ui")]] RWTexture2D<uint> tex_car_dirt_state_uav : register(u91);
[[vk::image_format("r32ui")]] RWTexture2D<uint> tex_car_dirt_wear_uav  : register(u92);

[numthreads(64, 1, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint count = pass_uint(pass_car_rain::count);
    if (thread_id.x >= count)
        return;

    CarDirtTexelData change = car_dirt_texels[pass_uint(pass_car_rain::offset) + thread_id.x];
    uint width              = pass_uint(pass_car_rain::atlas_width);
    uint2 texel             = uint2(change.texel % width, change.texel / width);
    if (texel.y >= pass_uint(pass_car_rain::atlas_height))
        return;

    tex_car_dirt_color_uav[texel] = change.color;
    tex_car_dirt_state_uav[texel] = change.state;
    tex_car_dirt_wear_uav[texel]  = change.wear;
}
