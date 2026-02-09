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

//= INCLUDES =========================
#include "Component.h"
#include <atomic>
#include <unordered_map>
#include "../../RHI/RHI_Definitions.h"
#include "../../Math/Quaternion.h"
//====================================

namespace spartan
{
    class Mesh;
    class Material;
    namespace math
    {
        class Vector3;
    }

    enum class TerrainProp
    {
        Tree,
        Grass,
        Flower,
        Rock,
        Max
    };

    struct TerrainPropDescription
    {
        bool  align_to_surface_normal  = true;
        float max_slope_angle_rad      = math::deg_to_rad * 35.0f;
        float surface_offset           = 0.05f;
        float min_spawn_height         = 0.0f;
        float max_spawn_height         = 1000.0f;
        float min_scale                = 0.8f;
        float max_scale                = 1.2f;
        bool  scale_adjust_by_slope    = false;
        uint32_t instances_per_cluster = 0;
        float cluster_radius           = 0.0f;
    };

    // precomputed per-triangle data for prop placement
    struct TriangleData
    {
        math::Vector3 normal;
        math::Vector3 v0;
        math::Vector3 v1_minus_v0;
        math::Vector3 v2_minus_v0;
        float slope_radians;
        float height_min;
        float height_max;
        math::Quaternion rotation_to_normal;
        math::Vector3 centroid;
    };

    class Terrain : public Component
    {
    public:
        Terrain(Entity* entity);
        ~Terrain();


        // height map
        RHI_Texture* GetHeightMapSeed() const          { return m_height_map_seed; }
        void SetHeightMapSeed(RHI_Texture* height_map) { m_height_map_seed = height_map;}
        RHI_Texture* GetHeightMapFinal() const         { return m_height_map_final.get(); }

        // dimensions
        uint32_t GetWidth() const  { return m_width; }
        uint32_t GetHeight() const { return m_height; }

        // height range
        float GetMinY() const     { return m_min_y; }
        void SetMinY(float min_z) { m_min_y = min_z; }
        float GetMaxY() const     { return m_max_y; }
        void SetMaxY(float max_z) { m_max_y = max_z; }

        // parameters - world levels
        float GetSeaLevel() const          { return m_level_sea; }
        void SetSeaLevel(float level)      { m_level_sea = level; }
        float GetSnowLevel() const         { return m_level_snow; }
        void SetSnowLevel(float level)     { m_level_snow = level; }

        // parameters - mesh generation
        uint32_t GetSmoothingPasses() const       { return m_smoothing; }
        void SetSmoothingPasses(uint32_t passes)  { m_smoothing = passes; }
        uint32_t GetDensity() const               { return m_density; }
        void SetDensity(uint32_t density)         { m_density = density; }
        uint32_t GetScale() const                 { return m_scale; }
        void SetScale(uint32_t scale)             { m_scale = scale; }
        bool GetCreateBorder() const              { return m_create_border; }
        void SetCreateBorder(bool create)         { m_create_border = create; }

        // stats
        float GetArea() const                   { return m_area_km2; }
        uint32_t GetVertexCount() const         { return m_vertex_count; }
        uint32_t GetIndexCount() const          { return m_index_count; }
        uint64_t GetHeightSampleCount() const   { return m_height_samples; }
        float* GetHeightData()                  { return !m_height_data.empty() ? &m_height_data[0] : nullptr; }
        std::shared_ptr<Material> GetMaterial() { return m_material; }

        // generation
        void Generate();
        void FindTransforms(
            const uint32_t tile_index,
            const TerrainProp terrain_prop,
            Entity* entity,
            const float density_fraction,
            const float scale,
            std::vector<math::Matrix>& transforms_out
        );

        // component io
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        // cache io
        void SaveToFile(const char* file_path);
        void LoadFromFile(const char* file_path);

        // triangle data access for placement system
        std::unordered_map<uint64_t, std::vector<TriangleData>>& GetTriangleData() { return m_triangle_data; }

    private:
        void Clear();
        uint64_t ComputeCacheHash() const;

        // textures
        RHI_Texture* m_height_map_seed                  = nullptr;
        std::shared_ptr<RHI_Texture> m_height_map_final = nullptr;

        // configurable parameters
        float m_min_y          = -64.0f;
        float m_max_y          = 256.0f;
        float m_level_sea      = 0.0f;
        float m_level_snow     = 400.0f;
        uint32_t m_smoothing   = 0;
        uint32_t m_density     = 3;
        uint32_t m_scale       = 6;
        bool m_create_border   = true;

        // runtime state
        uint32_t m_width                  = 0;
        uint32_t m_height                 = 0;
        float m_area_km2                  = 0.0f;
        std::atomic<bool> m_is_generating = false;
        uint32_t m_height_samples         = 0;
        uint32_t m_vertex_count           = 0;
        uint32_t m_index_count            = 0;
        uint32_t m_triangle_count         = 0;
        uint32_t m_dense_width            = 0;
        uint32_t m_dense_height           = 0;

        // geometry data
        std::vector<float> m_height_data;
        std::vector<std::vector<RHI_Vertex_PosTexNorTan>> m_tile_vertices;
        std::vector<RHI_Vertex_PosTexNorTan> m_vertices;
        std::vector<uint32_t> m_indices;
        std::vector<std::vector<uint32_t>> m_tile_indices;
        std::shared_ptr<Mesh> m_mesh;
        std::shared_ptr<Material> m_material;
        std::vector<math::Vector3> m_tile_offsets;
        std::vector<math::Vector3> m_positions;

        // placement data (per-terrain, not static)
        std::unordered_map<uint64_t, std::vector<TriangleData>> m_triangle_data;
    };
}
