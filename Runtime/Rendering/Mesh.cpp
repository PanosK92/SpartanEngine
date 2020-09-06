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

//= INCLUDES =================
#include "Spartan.h"
#include "Mesh.h"
#include "../RHI/RHI_Vertex.h"
//============================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    void Mesh::Clear()
    {
        m_vertices.clear();
        m_vertices.shrink_to_fit();
        m_indices.clear();
        m_indices.shrink_to_fit();
    }

    uint32_t Mesh::GetMemoryUsage() const
    {
        uint32_t size = 0;
        size += uint32_t(m_vertices.size()    * sizeof(RHI_Vertex_PosTexNorTan));
        size += uint32_t(m_indices.size()    * sizeof(uint32_t));

        return size;
    }

    void Mesh::GetGeometry(uint32_t indexOffset, uint32_t indexCount, uint32_t vertexOffset, unsigned vertexCount, vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices)
    {
        if ((indexOffset == 0 && indexCount == 0) || (vertexOffset == 0 && vertexCount == 0) || !vertices || !indices)
        {
            LOG_ERROR("Mesh::Geometry_Get: Invalid parameters");
            return;
        }

        // Indices
        const auto indexFirst   = m_indices.begin() + indexOffset;
        const auto indexLast    = m_indices.begin() + indexOffset + indexCount;
        *indices                = vector<uint32_t>(indexFirst, indexLast);

        // Vertices
        const auto vertexFirst  = m_vertices.begin() + vertexOffset;
        const auto vertexLast   = m_vertices.begin() + vertexOffset + vertexCount;
        *vertices               = vector<RHI_Vertex_PosTexNorTan>(vertexFirst, vertexLast);
    }

    void Mesh::Vertices_Append(const vector<RHI_Vertex_PosTexNorTan>& vertices, uint32_t* vertexOffset)
    {
        if (vertexOffset)
        {
            *vertexOffset = static_cast<uint32_t>(m_vertices.size());
        }

        m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
    }

    uint32_t Mesh::Vertices_Count() const
    {
        return static_cast<uint32_t>(m_vertices.size());
    }

    void Mesh::Vertex_Add(const RHI_Vertex_PosTexNorTan& vertex)
    {
        m_vertices.emplace_back(vertex);
    }

    void Mesh::Indices_Append(const vector<uint32_t>& indices, uint32_t* indexOffset)
    {
        if (indexOffset)
        {
            *indexOffset = static_cast<uint32_t>(m_indices.size());
        }

        m_indices.insert(m_indices.end(), indices.begin(), indices.end());
    }
}
