/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==============
#include "../math/Vector3.h"
#include "../math/Vector4.h"
//=========================

namespace spartan
{
    class RHI_Texture;
    class Water;

    // breaking surf where the fft ocean meets the terrain, see data/shaders/common_ocean_shore.hlsl
    // a shoreline field (signed distance to the coast, shoreward direction, beach slope) is built on a
    // worker thread whenever the terrain or the sea level changes, the gpu samples it as a texture and
    // buoyancy evaluates the same waves on the cpu
    namespace ocean_shore
    {
        void tick(const Water* water);
        void reset();
        void shutdown(); // releases the gpu texture, call before the device is destroyed

        RHI_Texture* get_texture();
        math::Vector4 get_mapping(); // xy = world xz of the texture origin, zw = 1 / world size
        math::Vector4 get_wave();    // x = swell height, y = period, z = wrapped time, w = enabled
        math::Vector4 get_swell();   // xy = travel direction, z = reach offshore

        // shore displacement at an fft grid point and the world y of the swash sheet, false without a field
        bool evaluate(float grid_x, float grid_z, float sea_level, math::Vector3& displacement, float& floor_y);
    }
}
