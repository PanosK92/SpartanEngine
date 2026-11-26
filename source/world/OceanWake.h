/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include <vector>
#include <cstdint>

namespace spartan
{
    // a vertical wetted slice of a body interacting with the sea, world space
    struct OceanWakeColumn
    {
        float x        = 0.0f;
        float z        = 0.0f;
        float volume   = 0.0f;  // m^3 of water displaced by this slice
        float radius   = 0.35f; // physical waterline footprint radius (m)
        float vx       = 0.0f;  // horizontal velocity (m/s)
        float vy       = 0.0f;  // vertical velocity (m/s)
        float vz       = 0.0f;
        float aeration = 0.0f;  // extra turbulent bubble / prop-wash injection (0..1)
    };

    // static waterline obstacle (e.g. pier piling, rock, seawall) that reflects and diffracts waves
    struct OceanWakeObstacle
    {
        float min_x    = 0.0f;
        float max_x    = 0.0f;
        float min_z    = 0.0f;
        float max_z    = 0.0f;
        float min_y    = 0.0f;
        float max_y    = 0.0f;
        bool is_circle = false;
    };

    // unified interactive fluid dynamics on top of the fft ocean, a camera-centered toroidal dispersive
    // wave field shared by boats, floating rigid bodies, the swimming player, vehicles, marine life,
    // and static waterline obstacles
    //
    // moving bodies inject velocity-driven potential-flow dipoles (bow wave + stern trough + lateral
    // parting), radial plunge impulses (splash crowns + concentric dispersive rings), and turbulent
    // surface/subsurface aeration; physics and rendering sample the exact same field
    class OceanWake
    {
    public:
        static constexpr uint32_t resolution = 512;
        static constexpr float cell_size     = 0.25f;
        static constexpr float window_size   = resolution * cell_size;

        // approximate the exponential decay of a submerged disturbance with depth, using its footprint as wavelength
        static float SurfaceInfluence(float water_above_top, float footprint);

        static bool IsEnabled();
        static void Clear();

        // updates a body's wetted columns; call while touching the sea, count 0 removes it immediately
        static void Displace(uint64_t id, const OceanWakeColumn* columns, uint32_t count);

        // registers or removes a static obstacle that reflects waves when it pierces the sea surface
        static void RegisterObstacle(uint64_t id, const OceanWakeObstacle& obstacle);
        static void UnregisterObstacle(uint64_t id);

        // recenters the toroidal window on the focus and steps the dispersive fluid solver once per frame
        static void Tick(float dt, const math::Vector3& focus, float sea_level);

        // metres the interactive fluid lifts the ocean surface at world xz, bilinear like the gpu
        static float SampleHeight(float world_x, float world_z);
        // xz holds count pairs of world x and z
        static void SampleHeights(const float* xz, float* heights, uint32_t count);
        // horizontal surface current (m/s) carried by interactive wakes at world xz
        static math::Vector2 SampleVelocity(float world_x, float world_z);
        // batched height + velocity lookup under a single lock for multi-column buoyant bodies
        static void SampleBatch(const float* xz, float* heights, math::Vector2* velocities, uint32_t count);

        // rgba16 float texels of the window: r = height (m), g = surface foam, b = subsurface bubbles, a = turbulence
        // repacks only changed tiles into texels and reports the dirty row band [row_start, row_start + row_count) for buffer_index (0 or 1)
        static bool CollectUpload(std::vector<uint16_t>& texels, uint32_t buffer_index, uint32_t& row_start, uint32_t& row_count);

        // xy = window min xz, z = 1 / window size, w = 1 while waves are active
        static math::Vector4 GetMapping();
    };
}
