/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include <cstdint>
#include <string>
//=================

namespace spartan
{
    class RHI_Texture;

    // measured light distributions (ies lm-63 files) for spot lights
    // every profile is resampled into a square tile of one shared atlas, tiles are stacked vertically
    // a tile spans the spot's shadow frustum in the light's right/up/forward frame:
    // u = (x / z) / tan(extent), v = (y / z) / tan(extent), both in [-1, 1], values are candela / peak
    // photometric type c puts the nadir on the light's forward axis, c0 on its right and c90 on its up
    // types a and b take positive horizontal angles to the right and positive vertical angles up,
    // type a (automotive) tilts its planes about the lateral axis: dir = (sin h, cos h sin v, cos h cos v)
    // type b tilts its planes about the vertical axis:              dir = (sin h cos v, sin v, cos h cos v)
    struct IesProfileInfo
    {
        std::string file_path;
        float extent_rad   = 0.0f; // half angle of the tile, a spot using the profile takes it as its angle
        float solid_angle  = 0.0f; // integral of the peak normalized profile over the tile, steradians
        float peak_candela = 0.0f; // brightest direction in the file
        float lumens       = 0.0f; // flux the file emits inside the tile, peak_candela * solid_angle
    };

    namespace ies
    {
        constexpr uint32_t tile_size = 1024;
        constexpr uint32_t max_profiles = 16; // 16 tiles of 1024 reach the 16384 texture height limit

        // loads a file once and returns its 1 based atlas tile, 0 when the file cannot be used
        uint32_t acquire(const std::string& file_path);
        const IesProfileInfo* get_info(uint32_t slot);

        // null until the first profile is acquired
        RHI_Texture* get_atlas();
        void shutdown();
    }
}
