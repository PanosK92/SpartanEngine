/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ========================
#include "IComponent.h"
#include <atomic>
#include "../../RHI/RHI_Definition.h"
//===================================

namespace Spartan
{
    class Model;
    namespace Math
    {
        class Vector3;
    }

    class SPARTAN_CLASS Terrain : public IComponent
    {
    public:
        Terrain(Context* context, Entity* entity, uint32_t id = 0);
        ~Terrain() = default;

        //= IComponent ===============================
        void OnInitialize() override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;
        //============================================

        const auto& GetHeightMap() const { return m_height_map; }
        void SetHeightMap(const std::shared_ptr<RHI_Texture2D>& height_map);

        float GetMinY() const { return m_min_y; }
        void SetMinY(float min_z)   { m_min_y = min_z; }

        float GetMaxY() const { return m_max_y; }
        void SetMaxY(float max_z)   { m_max_y = max_z; }

        float GetProgress() const { return static_cast<float>(static_cast<double>(m_progress_jobs_done) / static_cast<double>(m_progress_job_count)); }
        const auto& GetProgressDescription() const { return m_progress_desc; }

        void GenerateAsync();

    private:
        bool GeneratePositions(std::vector<Math::Vector3>& positions, const std::vector<std::byte>& height_map);
        bool GenerateVerticesIndices(const std::vector<Math::Vector3>& positions, std::vector<uint32_t>& indices, std::vector<RHI_Vertex_PosTexNorTan>& vertices);
        bool GenerateNormalTangents(const std::vector<uint32_t>& indices, std::vector<RHI_Vertex_PosTexNorTan>& vertices);
        void UpdateFromModel(const std::shared_ptr<Model>& model) const;
        void UpdateFromVertices(const std::vector<uint32_t>& indices, std::vector<RHI_Vertex_PosTexNorTan>& vertices);

        uint32_t m_width                            = 0;
        uint32_t m_height                           = 0;
        float m_min_y                               = 0.0f;
        float m_max_y                               = 30.0f;
        float m_vertex_density                      = 1.0f;
        bool m_is_generating                        = false;
        uint64_t m_vertex_count                     = 0;
        uint64_t m_face_count                       = 0;
        std::atomic<uint64_t> m_progress_jobs_done  = 0;
        uint64_t m_progress_job_count               = 1; // avoid devision by zero in GetProgress()
        std::string m_progress_desc;
        std::shared_ptr<RHI_Texture2D> m_height_map;
        std::shared_ptr<Model> m_model;
    };
}
