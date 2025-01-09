/*
Copyright(c) 2016-2025 Panos Karabelas

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
    // this namespace organizes 3D objects into a grid layout, grouping instances into grid cells
    // it enables optimized rendering by allowing culling of non-visible chunks efficiently

    const uint32_t physical_cell_size = 125;

    struct GridKey
    {
        uint32_t x, y, z;

        bool operator==(const GridKey& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct GridKeyHash
    {
        size_t operator()(const GridKey& k) const
        {
            // interleave the bits of the x, y, and z coordinates
            // this approach tends to maintain spatial locality better, as nearby
            // coordinates will result in hash values that are also close to each other

            size_t result = 0;

            for (uint32_t i = 0; i < (sizeof(uint32_t) * 8); i++) // for each bit in the integers
            {
                result |= ((k.x & (1 << i)) << (2 * i)) | ((k.y & (1 << i)) << ((2 * i) + 1)) | ((k.z & (1 << i)) << ((2 * i) + 2));
            }

            return result;
        }

        static GridKey get_key(const spartan::math::Vector3& position)
        {
            return
            {
                static_cast<uint32_t>(std::floor(position.x / static_cast<float>(physical_cell_size))),
                static_cast<uint32_t>(std::floor(position.y / static_cast<float>(physical_cell_size))),
                static_cast<uint32_t>(std::floor(position.z / static_cast<float>(physical_cell_size)))
            };
        }
    };

    void reorder_instances_into_cell_chunks(std::vector<spartan::math::Matrix>& instance_transforms, std::vector<uint32_t>& cell_end_indices)
    {
        // populate the grid map
        std::unordered_map<GridKey, std::vector<spartan::math::Matrix>, GridKeyHash> grid_map;
        for (const auto& instance : instance_transforms)
        {
            spartan::math::Vector3 position = instance.GetTranslation();

            GridKey key = GridKeyHash::get_key(position);
            grid_map[key].push_back(instance);
        }

        // reorder instances based on grid map
        instance_transforms.clear();
        cell_end_indices.clear();
        uint32_t index = 0;
        for (const auto& [key, transforms] : grid_map)
        {
            instance_transforms.insert(instance_transforms.end(), transforms.begin(), transforms.end());
            index += static_cast<uint32_t>(transforms.size());
            cell_end_indices.push_back(index);
        }
    }
}
