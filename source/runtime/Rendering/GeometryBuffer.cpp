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

//= INCLUDES ====================
#include "pch.h"
#include "GeometryBuffer.h"
#include "../RHI/RHI_Buffer.h"
//===============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // static member definitions
    vector<RHI_Vertex_PosTexNorTan> GeometryBuffer::m_vertices;
    vector<uint32_t> GeometryBuffer::m_indices;
    unique_ptr<RHI_Buffer> GeometryBuffer::m_vertex_buffer;
    unique_ptr<RHI_Buffer> GeometryBuffer::m_index_buffer;
    uint32_t GeometryBuffer::m_vertex_count_committed = 0;
    uint32_t GeometryBuffer::m_index_count_committed  = 0;
    uint32_t GeometryBuffer::m_vertex_capacity        = 0;
    uint32_t GeometryBuffer::m_index_capacity         = 0;
    bool GeometryBuffer::m_dirty                      = false;
    bool GeometryBuffer::m_was_rebuilt                = false;
    mutex GeometryBuffer::m_mutex;

    uint32_t GeometryBuffer::AppendVertices(const RHI_Vertex_PosTexNorTan* data, uint32_t count)
    {
        lock_guard<mutex> lock(m_mutex);

        uint32_t base_offset = static_cast<uint32_t>(m_vertices.size());
        m_vertices.insert(m_vertices.end(), data, data + count);
        m_dirty = true;

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendIndices(const uint32_t* data, uint32_t count)
    {
        lock_guard<mutex> lock(m_mutex);

        uint32_t base_offset = static_cast<uint32_t>(m_indices.size());
        m_indices.insert(m_indices.end(), data, data + count);
        m_dirty = true;

        return base_offset;
    }

    void GeometryBuffer::BuildIfDirty()
    {
        lock_guard<mutex> lock(m_mutex);

        if (!m_dirty || m_vertices.empty() || m_indices.empty())
            return;

        uint32_t vertex_count = static_cast<uint32_t>(m_vertices.size());
        uint32_t index_count  = static_cast<uint32_t>(m_indices.size());

        m_was_rebuilt            = false;
        bool needs_full_rebuild = !m_vertex_buffer || !m_index_buffer || vertex_count > m_vertex_capacity || index_count > m_index_capacity;

        if (needs_full_rebuild)
        {
            // destroy existing gpu buffers before creating new ones
            m_vertex_buffer = nullptr;
            m_index_buffer  = nullptr;

            // allocate with headroom so late-arriving meshes don't trigger another rebuild
            m_vertex_capacity = static_cast<uint32_t>(vertex_count * growth_factor);
            m_index_capacity  = static_cast<uint32_t>(index_count * growth_factor);

            // create vertex buffer with capacity (no initial data - we upload via sub-region)
            m_vertex_buffer = make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Vertex,
                sizeof(RHI_Vertex_PosTexNorTan),
                m_vertex_capacity,
                nullptr, // no initial data
                false,
                "geometry_buffer_vertex"
            );

            // create index buffer with capacity (no initial data)
            m_index_buffer = make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Index,
                sizeof(uint32_t),
                m_index_capacity,
                nullptr, // no initial data
                false,
                "geometry_buffer_index"
            );

            // upload all committed data into the newly allocated buffers
            m_vertex_buffer->UploadSubRegion(m_vertices.data(), 0, vertex_count * sizeof(RHI_Vertex_PosTexNorTan));
            m_index_buffer->UploadSubRegion(m_indices.data(), 0, index_count * sizeof(uint32_t));

            m_vertex_count_committed = vertex_count;
            m_index_count_committed  = index_count;

            m_was_rebuilt = true;

            SP_LOG_INFO("Global geometry buffer built: %u vertices (%.2f MB), %u indices (%.2f MB), capacity: %u vertices, %u indices",
                vertex_count,
                (vertex_count * sizeof(RHI_Vertex_PosTexNorTan)) / (1024.0f * 1024.0f),
                index_count,
                (index_count * sizeof(uint32_t)) / (1024.0f * 1024.0f),
                m_vertex_capacity,
                m_index_capacity
            );
        }
        else
        {
            // the new data fits within the pre-allocated capacity, upload only the new portion
            uint32_t new_vertices = vertex_count - m_vertex_count_committed;
            uint32_t new_indices  = index_count - m_index_count_committed;

            if (new_vertices > 0)
            {
                uint64_t offset = static_cast<uint64_t>(m_vertex_count_committed) * sizeof(RHI_Vertex_PosTexNorTan);
                uint64_t size   = static_cast<uint64_t>(new_vertices) * sizeof(RHI_Vertex_PosTexNorTan);
                m_vertex_buffer->UploadSubRegion(m_vertices.data() + m_vertex_count_committed, offset, size);
            }

            if (new_indices > 0)
            {
                uint64_t offset = static_cast<uint64_t>(m_index_count_committed) * sizeof(uint32_t);
                uint64_t size   = static_cast<uint64_t>(new_indices) * sizeof(uint32_t);
                m_index_buffer->UploadSubRegion(m_indices.data() + m_index_count_committed, offset, size);
            }

            m_vertex_count_committed = vertex_count;
            m_index_count_committed  = index_count;

            SP_LOG_INFO("Global geometry buffer updated: +%u vertices, +%u indices (sub-region upload, no rebuild)",
                new_vertices, new_indices
            );
        }

        m_dirty = false;
    }

    bool GeometryBuffer::WasRebuilt()
    {
        bool result  = m_was_rebuilt;
        m_was_rebuilt = false;
        return result;
    }

    void GeometryBuffer::Shutdown()
    {
        m_vertex_buffer = nullptr;
        m_index_buffer  = nullptr;
        m_vertices.clear();
        m_vertices.shrink_to_fit();
        m_indices.clear();
        m_indices.shrink_to_fit();
        m_vertex_count_committed = 0;
        m_index_count_committed  = 0;
        m_vertex_capacity        = 0;
        m_index_capacity         = 0;
        m_dirty                  = false;
        m_was_rebuilt            = false;
    }

    RHI_Buffer* GeometryBuffer::GetVertexBuffer()
    {
        return m_vertex_buffer.get();
    }

    RHI_Buffer* GeometryBuffer::GetIndexBuffer()
    {
        return m_index_buffer.get();
    }
}
