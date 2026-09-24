// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once

#include <array>
#include <memory>
#include <vector>

namespace spartan::navigation
{
    using Point = std::array<float, 3>;

    // One humanoid profile. Tile coordinates cover the whole world, including negative coordinates.
    constexpr float tile_size = 64.0f;
    constexpr float cell_size = 0.4f;
    constexpr float cell_height = 0.2f;
    constexpr float agent_radius = 0.4f;
    constexpr float agent_height = 1.8f;
    constexpr float agent_climb = 0.4f;
    constexpr float tile_border = 2.0f; // (ceil(radius / cell_size) + 3) cells, rounded up

    struct TileInput
    {
        int x = 0, z = 0;
        float min_y = 0, max_y = 0;
        std::vector<float> vertices;
        std::vector<int> indices;
    };

    struct TileData
    {
        int x = 0, z = 0;
        bool succeeded = false; // an empty, successfully built tile is valid (sea, steep ground)
        std::vector<unsigned char> bytes;
    };

    // No scene pointers or GPU/physics dependencies. BuildTile can run on a worker; all other calls
    // belong to the main thread. Detour owns installed tile data and crowd agent path corridors.
    class NavigationMesh
    {
    public:
        NavigationMesh();
        ~NavigationMesh();
        NavigationMesh(const NavigationMesh&) = delete;
        NavigationMesh& operator=(const NavigationMesh&) = delete;

        static TileData BuildTile(const TileInput& input);
        bool IsReady() const;
        bool Install(const TileData& tile);
        void Remove(int x, int z);
        bool Project(const Point& position, Point& projected) const;
        int AddAgent(const Point& position, float speed);
        void RemoveAgent(int agent);
        bool GetAgent(int agent, Point& position, Point& velocity) const;
        bool Wander(int agent, float radius);
        bool MoveTo(int agent, const Point& destination);
        void Update(float delta_time);
        void GetDebugTriangles(std::vector<Point>& triangles) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
