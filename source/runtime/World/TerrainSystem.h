/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES =================
#include <vector>
#include <cstdint>
#include "../Math/Vector3.h"
#include "../Math/Ray.h"
#include "../RHI/RHI_Vertex.h"
//============================

namespace spartan
{
    class RHI_Texture;

    enum class TerrainBrushMode
    {
        Raise,
        Lower,
        Smooth,
        Flatten
    };

    struct TerrainBrush
    {
        TerrainBrushMode mode = TerrainBrushMode::Raise;
        float radius          = 50.0f;
        float strength        = 5.0f;
        float falloff         = 0.5f;
        float target_height   = 0.0f;
    };

    // grid extents derived from density and scale, shared by generation and sculpting
    struct TerrainGridMapping
    {
        float scale_x  = 1.0f;
        float scale_z  = 1.0f;
        float offset_x = 0.0f;
        float offset_z = 0.0f;
        float extent_x = 0.0f;
        float extent_z = 0.0f;
    };

    // core terrain algorithms, heightfield ops, and brush sculpting
    class TerrainSystem
    {
    public:
        static TerrainGridMapping ComputeGridMapping(
            uint32_t dense_width,
            uint32_t dense_height,
            uint32_t density,
            uint32_t scale
        );

        static void GetValuesFromHeightMap(
            std::vector<float>& height_data_out,
            RHI_Texture* height_texture,
            float min_y,
            float max_y,
            uint32_t smoothing,
            bool create_border
        );

        static void DensifyHeightMap(
            std::vector<float>& height_data,
            uint32_t width,
            uint32_t height,
            uint32_t density
        );

        static void GeneratePositions(
            std::vector<math::Vector3>& positions,
            const std::vector<float>& height_map,
            uint32_t width,
            uint32_t height,
            uint32_t density,
            uint32_t scale
        );

        static void ApplyPerlinNoise(
            std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            float amplitude = 5.0f,
            float frequency = 0.01f,
            uint32_t octaves = 4,
            float persistence = 1.0f
        );

        static void ApplyErosion(
            std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            float level_sea,
            uint32_t iterations = 1'000'000,
            uint32_t wind_interval = 50'000
        );

        static void GenerateVerticesAndIndices(
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            std::vector<uint32_t>& indices,
            const std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height
        );

        static void GenerateNormals(
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            uint32_t width,
            uint32_t height
        );

        static float ComputeSurfaceAreaKm2(
            const std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            const std::vector<uint32_t>& indices
        );

        // create a flat heightfield at sea_level
        static void CreateFlatHeightfield(
            std::vector<float>& height_data_out,
            std::vector<math::Vector3>& positions_out,
            uint32_t base_width,
            uint32_t base_height,
            uint32_t density,
            uint32_t scale,
            float sea_level
        );

        static float SampleHeight(
            const std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            float world_x,
            float world_z,
            const TerrainGridMapping& mapping
        );

        static bool RaycastHeightfield(
            const math::Ray& ray,
            const std::vector<math::Vector3>& positions,
            uint32_t width,
            uint32_t height,
            const TerrainGridMapping& mapping,
            math::Vector3& hit_out,
            float max_distance = 10000.0f
        );

        // mutate positions around world_center, also syncs height_data if provided
        static void ApplyBrush(
            std::vector<math::Vector3>& positions,
            std::vector<float>* height_data,
            uint32_t width,
            uint32_t height,
            const TerrainGridMapping& mapping,
            const math::Vector3& world_center,
            const TerrainBrush& brush
        );

        static void SyncHeightDataFromPositions(
            std::vector<float>& height_data,
            const std::vector<math::Vector3>& positions
        );
    };
}
