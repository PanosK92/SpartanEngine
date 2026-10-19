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
    // one loaded tire patch over one physics step, world space
    struct TerrainDeformationStamp
    {
        math::Vector3 position_previous;
        math::Vector3 position;
        math::Vector3 forward;          // wheel heading
        math::Vector3 normal;           // ground normal
        float width    = 0.25f;         // tread width, m
        float radius   = 0.33f;         // m
        float load     = 0.0f;          // N
        float pressure = 2.2f;          // inflation, bar gauge
        float tread_speed = 0.0f;       // m/s, wheel spin times radius, tread outrunning the ground digs
        float dt       = 0.0f;
    };

    // one footfall, world space
    struct TerrainDeformationFoot
    {
        math::Vector3 position;         // under the sole
        math::Vector3 forward;          // walking direction
        float load = 0.0f;              // N, the peak the sole carries
        float side = 1.0f;              // 1 right foot, -1 left
        float push = 0.0f;              // 0 a heel strike walking, 1 a forefoot push off running
    };

    // where the ground is loose sand, the height and slope band of the sand terrain layer, world space
    struct TerrainDeformationSand
    {
        float sea_level  = 0.0f;
        float height_min = 0.0f;        // m above sea level
        float height_max = 0.0f;
        float slope_min  = 0.0f;        // degrees
        float slope_max  = 0.0f;
        bool valid       = false;
    };

    // a mip 0 sub rectangle of the gpu field, r16 float texels
    struct TerrainDeformationUpload
    {
        uint32_t x      = 0;
        uint32_t y      = 0;
        uint32_t width  = 0;
        uint32_t height = 0;
        std::vector<uint16_t> texels;
    };

    // transient tire ruts in soft ground, a toroidal grid that follows the camera
    //
    // the grid stores the signed offset of the real ground from the collision and render surface,
    // negative inside a rut, positive on the soil the tire pushed aside, cells that scroll out of
    // the window are erased, nothing is saved, physics and rendering read the same numbers
    //
    // physics stamps and samples from the simulation thread, the renderer collects uploads and
    // moves the window from the main thread, every access goes through one lock
    class TerrainDeformation
    {
    public:
        static constexpr uint32_t resolution = 1024;
        static constexpr float cell_size      = 0.0625f;
        static constexpr float window_size    = resolution * cell_size;

        static bool IsEnabled();
        static void Clear();

        // recenters the window around a world position, erases what falls out of it
        static void SetFocus(const math::Vector3& position);

        // presses a tire into the ground, returns the compaction resistance in newtons, the
        // force it takes to push a fresh rut, rolling in an existing one costs almost nothing
        static float Stamp(const TerrainDeformationStamp& stamp);

        // the sand band the soil model reads, dry sand gives deep and slumping ruts, the swash packs it firm
        static void SetSand(const TerrainDeformationSand& sand);

        // strides a walker along and presses a footprint into sand at every step, feet is the ground point
        // under the body, call once per frame
        static void Walk(uint64_t id, const math::Vector3& feet, const math::Vector3& velocity, bool grounded, float mass);
        static float StampFoot(const TerrainDeformationFoot& foot);

        // relaxes ruts and prints back to flat where the swash runs over them, depth returns the water sheet
        // thickness at a world xz, call once per frame
        static void Wash(float dt, float (*swash_depth)(float world_x, float world_z));

        // signed ground offset at a world xz, bilinear like the gpu reads it
        static float SampleOffset(float world_x, float world_z);

        // dirty texels since the last call, false when nothing changed
        static bool CollectUploads(std::vector<TerrainDeformationUpload>& uploads);

        // xy = window min xz, z = 1 / window size, w = 1 when anything is stamped
        static math::Vector4 GetMapping();
        // xy = min xz, zw = max xz of every stamped cell, used to cull and tessellate
        static math::Vector4 GetBounds();
        static bool HasContent();

        // terrain.deformation_debug 1 logs stamp counts and the deepest cell about once a second
        static void LogStats(bool gpu_ready);
    };
}
