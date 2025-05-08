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

//= INCLUDES =========================
#include "Component.h"
#include <atomic>
#include "../../RHI/RHI_Definitions.h"
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
        Grass
    };

    class Terrain : public Component
    {
    public:
        Terrain(Entity* entity);
        ~Terrain();

        RHI_Texture* GetHeightMap() const          { return m_height_texture; }
        void SetHeightMap(RHI_Texture* height_map) { m_height_texture = height_map;}

        uint32_t GetWidth() const;
        uint32_t GetHeight() const;

        float GetMinY() const     { return m_min_y; }
        void SetMinY(float min_z) { m_min_y = min_z; }

        float GetMaxY() const     { return m_max_y; }
        void SetMaxY(float max_z) { m_max_y = max_z; }

        float GetArea() const     { return m_area_km2; }

        // generate
        void Generate();
        void GenerateTransforms(std::vector<math::Matrix>* transforms, const uint32_t count, const TerrainProp terrain_prop, float offset_y = 0.0f);

        // io
        void SaveToFile(const char* file_path);
        void LoadFromFile(const char* file_path);

        uint32_t GetVertexCount() const         { return m_vertex_count; }
        uint32_t GetIndexCount() const          { return m_index_count; }
        uint64_t GetHeightSampleCount() const   { return m_height_samples; }
        float* GetHeightData()                  { return !m_height_data.empty() ? &m_height_data[0] : nullptr; }
        std::shared_ptr<Material> GetMaterial() { return m_material; }
 
    private:
        void Clear();

        float m_min_y                     = -64.0f; // sea level is 0.0 - engine axiom
        float m_max_y                     = 256.0f;
        float m_area_km2                  = 0.0f;
        std::atomic<bool> m_is_generating = false;
        uint32_t m_height_samples         = 0;
        uint32_t m_vertex_count           = 0;
        uint32_t m_index_count            = 0;
        uint32_t m_triangle_count         = 0;
        RHI_Texture* m_height_texture     = nullptr;
        std::vector<float> m_height_data;
        std::vector<std::vector<RHI_Vertex_PosTexNorTan>> m_tile_vertices;
        std::vector<RHI_Vertex_PosTexNorTan> m_vertices;
        std::vector<uint32_t> m_indices;
        std::vector<std::vector<uint32_t>> m_tile_indices;
        std::shared_ptr<Mesh> m_mesh;
        std::shared_ptr<Material> m_material;
    };
}
