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

//= INCLUDES =====================
#include <vector>
#include "../RHI/RHI_Definition.h"
//================================

namespace Spartan
{
    class Mesh
    {
    public:
        Mesh() = default;
        ~Mesh() { Clear(); }

        // Geometry
        void Clear();
        void GetGeometry(
            uint32_t indexOffset,
            uint32_t indexCount,
            uint32_t vertexOffset,
            unsigned vertexCount,
            std::vector<uint32_t>* indices,
            std::vector<RHI_Vertex_PosTexNorTan>* vertices
        );
        uint32_t GetMemoryUsage() const;

        // Vertices
        void Vertex_Add(const RHI_Vertex_PosTexNorTan& vertex);
        void Vertices_Append(const std::vector<RHI_Vertex_PosTexNorTan>& vertices, uint32_t* vertexOffset);
        uint32_t Vertices_Count() const;
        std::vector<RHI_Vertex_PosTexNorTan>& Vertices_Get()                    { return m_vertices; }
        void Vertices_Set(const std::vector<RHI_Vertex_PosTexNorTan>& vertices) { m_vertices = vertices; }

        // Indices
        void Index_Add(uint32_t index)                          { m_indices.emplace_back(index); }
        std::vector<uint32_t>& Indices_Get()                    { return m_indices; }
        void Indices_Set(const std::vector<uint32_t>& indices)  { m_indices = indices; }
        uint32_t Indices_Count() const                          { return static_cast<uint32_t>(m_indices.size()); }
        void Indices_Append(const std::vector<uint32_t>& indices, uint32_t* indexOffset);
    
        // Misc
        uint32_t GetTriangleCount() const { return Indices_Count() / 3; }
        
    private:
        std::vector<RHI_Vertex_PosTexNorTan> m_vertices;
        std::vector<uint32_t> m_indices;
    };
}
