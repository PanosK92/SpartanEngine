/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "../../RHI/RHI_Definition.h"
#include <vector>
//===================================

namespace Spartan
{
    class Model;

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

        const auto& GetHeightMap()                                          { return m_height_map; }
        void SetHeightMap(const std::shared_ptr<RHI_Texture>& height_map)   { m_height_map = height_map; }

        float GetMinZ()             { return m_min_z; }
        void SetMinZ(float min_z)   { m_min_z = min_z; }

        float GetMaxZ()             { return m_max_z; }
        void SetMaxZ(float max_z)   { m_max_z = max_z; }

        float GetSmoothness()                   { return m_smoothness; }
        void SetSmoothness(float smoothness)    { m_smoothness = smoothness; }

        void Generate();

    private:
        void UpdateModel(const std::vector<uint32_t>& indices, std::vector<RHI_Vertex_PosTexNorTan>& vertices);
        void ComputeNormals(const std::vector<uint32_t>& indices, std::vector<RHI_Vertex_PosTexNorTan>& vertices);
        void ClearGeometry();
        void FreeMemory();

        uint32_t m_width    = 0;
        uint32_t m_height   = 0;
        float m_min_z       = 0.0f;
        float m_max_z       = 20.0f;
        float m_smoothness  = 1.0f;
        std::vector<RHI_Vertex_PosTexNorTan> m_vertices;
        std::vector<uint32_t> m_indices;
        std::shared_ptr<RHI_Texture> m_height_map;
        std::shared_ptr<Model> m_model;
    };
}
