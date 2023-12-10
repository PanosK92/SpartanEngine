/*
Copyright(c) 2016-2023 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES ===============
#include <unordered_map>
#include "../Math/Vector3.h"
#include "../Math/Matrix.h"
//==========================

namespace grid_partitioning
{
    // This namespace organizes 3D objects into a grid layout, grouping instances into grid cells.
    // It enables optimized rendering by allowing culling of non-visible chunks efficiently.

    const uint32_t physical_cell_Size = 200;

    struct GridKey
    {
        int x, y, z;

        bool operator==(const GridKey& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct GridKeyHash
    {
        size_t operator()(const GridKey& k) const
        {
            return ((std::hash<int>()(k.x) ^ (std::hash<int>()(k.y) << 1)) >> 1) ^ (std::hash<int>()(k.z) << 1);
        }
    };

    GridKey get_key(const Spartan::Math::Vector3& position)
    {
        return
        {
            static_cast<int>(std::floor(position.x / physical_cell_Size)),
            static_cast<int>(std::floor(position.y / physical_cell_Size)),
            static_cast<int>(std::floor(position.z / physical_cell_Size))
        };
    }

    void reorder_instances_into_cell_chunks(std::vector<Spartan::Math::Matrix>& instance_transforms, std::vector<uint32_t>& cell_end_indices)
    {
        // populate the grid map
        std::unordered_map<GridKey, std::vector<Spartan::Math::Matrix>, GridKeyHash> grid_map;
        for (const auto& instance : instance_transforms)
        {
            // the instance transforms are transposed (see terrain.cpp), so we need to transpose them back
            Spartan::Math::Matrix transform = instance.Transposed();
            Spartan::Math::Vector3 position = transform.GetTranslation();

            GridKey key = get_key(position);
            grid_map[key].push_back(instance);
        }

        instance_transforms.clear();
        cell_end_indices.clear();
        uint32_t index = 0;

        // reorder instances based on grid map
        for (const auto& [key, transforms] : grid_map)
        {
            instance_transforms.insert(instance_transforms.end(), transforms.begin(), transforms.end());
            index += static_cast<uint32_t>(transforms.size());
            cell_end_indices.push_back(index);
        }
    }
}
