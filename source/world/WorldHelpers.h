/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include <functional>

//= INCLUDES ===============
#include <sol/forward.hpp>
#include <vector>
#include <cstdint>
//==========================

namespace spartan
{
    class Entity;

    // engine side helpers exposed to the lua world builder scripts
    // these wrap the heavy multithreaded construction that used to live in game.cpp
    class WorldHelpers
    {
    public:
        // registers material, resource cache, renderer grass, geometry buffer and forest helpers with the lua state
        static void RegisterForScripting(sol::state_view state);

        // builds the procedural forest world, terrain, water, props and gpu grass
        static void BuildForest(Entity* builder_entity);

        // spawn biome-masked grass, rocks and trees on an existing terrain
        static void PopulateTerrainBiomeProps(class Terrain* terrain);
        // Main-thread steps; returns true when all camera-first tile batches are published.
        static std::function<bool()> BeginTerrainBiomeProps(class Terrain* terrain);

        // place the mesh layers again on the given tiles only, the rest of the map keeps its props
        static void RepopulateTerrainProps(class Terrain* terrain, const std::vector<uint32_t>& tile_indices);

        // rebind grass and micro detail to the current height map and biome mask, leaves cpu props alone
        static void RefreshTerrainGpuScatter(class Terrain* terrain);

        // sweeps every terrain prop out of the world, wherever it currently sits in the hierarchy
        static void RemoveTerrainProps();

        // releases the long lived meshes and materials owned by the builders, called from world shutdown
        static void Clear();
    };
}
