/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ===========================
#include "pch.h"
#include "Mesh.h"
#include "../RHI/RHI_Vertex.h"
SP_WARNINGS_OFF
#include "meshoptimizer/meshoptimizer.h"
SP_WARNINGS_ON
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    void Mesh::Clear()
    {
        m_indices.clear();
        m_indices.shrink_to_fit();

        m_vertices.clear();
        m_vertices.shrink_to_fit();
    }

    uint32_t Mesh::GetMemoryUsage() const
    {
        uint32_t size = 0;
        size += uint32_t(m_indices.size()  * sizeof(uint32_t));
        size += uint32_t(m_vertices.size() * sizeof(RHI_Vertex_PosTexNorTan));

        return size;
    }

    void Mesh::GetGeometry(uint32_t index_offset, uint32_t index_count, uint32_t vertex_offset, uint32_t vertex_count, vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices)
    {
        SP_ASSERT_MSG(indices != nullptr || vertices != nullptr, "Indices and vertices vectors can't both be null");

        if (indices)
        {
            SP_ASSERT_MSG(index_count != 0, "Index count can't be 0");

            const auto index_first = m_indices.begin() + index_offset;
            const auto index_last  = m_indices.begin() + index_offset + index_count;
            *indices               = vector<uint32_t>(index_first, index_last);
        }

        if (vertices)
        {
            SP_ASSERT_MSG(vertex_count != 0, "Index count can't be 0");

            const auto vertex_first = m_vertices.begin() + vertex_offset;
            const auto vertex_last  = m_vertices.begin() + vertex_offset + vertex_count;
            *vertices               = vector<RHI_Vertex_PosTexNorTan>(vertex_first, vertex_last);
        }
    }

    void Mesh::AddVertices(const vector<RHI_Vertex_PosTexNorTan>& vertices, uint32_t* vertexOffset)
    {
        if (vertexOffset)
        {
            *vertexOffset = static_cast<uint32_t>(m_vertices.size());
        }

        m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
    }

    void Mesh::AddIndices(const vector<uint32_t>& indices, uint32_t* indexOffset)
    {
        if (indexOffset)
        {
            *indexOffset = static_cast<uint32_t>(m_indices.size());
        }

        m_indices.insert(m_indices.end(), indices.begin(), indices.end());
    }

    uint32_t Mesh::GetVertexCount() const
    {
        return static_cast<uint32_t>(m_vertices.size());
    }

    uint32_t Mesh::GetIndexCount() const
    {
        return static_cast<uint32_t>(m_indices.size());
    }

    void Mesh::Optimize()
    {
        uint32_t index_count                     = static_cast<uint32_t>(m_indices.size());
        uint32_t vertex_count                    = static_cast<uint32_t>(m_vertices.size());
        size_t vertex_size                       = sizeof(RHI_Vertex_PosTexNorTan);
        vector<uint32_t> indices                 = m_indices;
        vector<RHI_Vertex_PosTexNorTan> vertices = m_vertices;

        // The optimization order is important

        // Vertex cache optimization - reordering triangles to maximize cache locality
        LOG_INFO("Optimizing vertex cache...");
        meshopt_optimizeVertexCache(&indices[0], &m_indices[0], index_count, vertex_count);

        // Overdraw optimizations - reorders triangles to minimize overdraw from all directions
        LOG_INFO("Optimizing overdraw...");
        meshopt_optimizeOverdraw(&m_indices[0], &indices[0], index_count, &m_vertices[0].pos[0], vertex_count, vertex_size, 1.05f);

        // Vertex fetch optimization - reorders triangles to maximize memory access locality
        LOG_INFO("Optimizing vertex fetch...");
        meshopt_optimizeVertexFetch(&m_vertices[0], &m_indices[0], index_count, &vertices[0], vertex_count, vertex_size);
    }
}
