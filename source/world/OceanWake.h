/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include <vector>
#include <cstdint>

namespace spartan
{
    // a vertical slice of a body in the sea, world space
    struct OceanWakeColumn
    {
        float x      = 0.0f;
        float z      = 0.0f;
        float volume = 0.0f; // m^3 of water the slice holds out of its place
    };

    // interactive water on top of the fft ocean, a toroidal height field that follows the camera
    //
    // bodies in the sea report the water they hold out of its place every physics step, the change of that
    // volume per cell is the source of a damped shallow water wave equation, so a hull ploughing forward
    // heaps a bow wave, leaves a trough and a v shaped wake behind, a body dropping in throws a ring out,
    // the waves slow down in the shallows and reflect off the beach, physics and rendering read the same field
    //
    // physics reports and samples from the simulation thread, the renderer steps and uploads from the
    // main thread, every access goes through one lock
    class OceanWake
    {
    public:
        static constexpr uint32_t resolution = 256;
        static constexpr float cell_size      = 0.25f;
        static constexpr float window_size    = resolution * cell_size;

        static bool IsEnabled();
        static void Clear();

        // replaces what a body holds out of the water, call every physics step while it touches the sea, count 0 removes it
        static void Displace(uint64_t id, const OceanWakeColumn* columns, uint32_t count);

        // recenters the window on the focus and steps the waves, call once per frame
        static void Tick(float dt, const math::Vector3& focus, float sea_level);

        // metres the wake lifts the ocean surface at a world xz, bilinear like the gpu reads it
        static float SampleHeight(float world_x, float world_z);
        // xz holds count pairs of world x and z
        static void SampleHeights(const float* xz, float* heights, uint32_t count);

        // rg16 float texels of the whole window, r = height, g = foam, false while the sea is still
        static bool CollectUpload(std::vector<uint16_t>& texels);

        // xy = window min xz, z = 1 / window size, w = 1 while waves are running
        static math::Vector4 GetMapping();
    };
}
