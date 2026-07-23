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
#include "../RHI/RHI_Device.h"
//===============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    // static member definitions
    vector<RHI_Vertex_PosTexNorTan> GeometryBuffer::m_vertices;
    vector<uint32_t> GeometryBuffer::m_indices;
    vector<Sb_MeshletBounds> GeometryBuffer::m_meshlet_bounds;
    vector<Instance> GeometryBuffer::m_instances;
    unique_ptr<RHI_Buffer> GeometryBuffer::m_vertex_buffer;
    unique_ptr<RHI_Buffer> GeometryBuffer::m_index_buffer;
    unique_ptr<RHI_Buffer> GeometryBuffer::m_meshlet_bounds_buffer;
    unique_ptr<RHI_Buffer> GeometryBuffer::m_instance_buffer;
    uint32_t GeometryBuffer::m_vertex_count_committed         = 0;
    uint32_t GeometryBuffer::m_index_count_committed          = 0;
    uint32_t GeometryBuffer::m_meshlet_bounds_count_committed = 0;
    uint32_t GeometryBuffer::m_instance_count_committed       = 0;
    uint32_t GeometryBuffer::m_vertex_capacity                = 0;
    uint32_t GeometryBuffer::m_index_capacity                 = 0;
    uint32_t GeometryBuffer::m_meshlet_bounds_capacity        = 0;
    uint32_t GeometryBuffer::m_instance_capacity              = 0;
    uint32_t GeometryBuffer::m_instance_capacity_failed_at    = 0;
    bool GeometryBuffer::m_dirty                              = false;
    bool GeometryBuffer::m_was_rebuilt                        = false;
    bool GeometryBuffer::m_oom_logged                         = false;
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

    uint32_t GeometryBuffer::AppendMeshletBounds(const Sb_MeshletBounds* data, uint32_t count)
    {
        lock_guard<mutex> lock(m_mutex);

        uint32_t base_offset = static_cast<uint32_t>(m_meshlet_bounds.size());
        if (count > 0)
        {
            m_meshlet_bounds.insert(m_meshlet_bounds.end(), data, data + count);
            m_dirty = true;
        }

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendInstances(const Instance* data, uint32_t count)
    {
        lock_guard<mutex> lock(m_mutex);

        // seed slot 0 with an identity instance so non-instanced draws can read identity at offset 0
        if (m_instances.empty())
        {
            m_instances.push_back(Instance::GetIdentity());
            m_dirty = true;
        }

        uint32_t base_offset = static_cast<uint32_t>(m_instances.size());

        // once an instance oom has been seen, refuse all further appends so the cpu vector never grows past the last gpu-resident size
        // also avoids the per-frame rebuild + log spam pattern (every async grass tile arrival would otherwise retry an alloc that already failed)
        if (m_instance_capacity_failed_at != 0)
        {
            return base_offset;
        }

        if (count > 0)
        {
            m_instances.insert(m_instances.end(), data, data + count);
            m_dirty = true;
        }

        return base_offset;
    }

    void GeometryBuffer::UpdateVertices(const RHI_Vertex_PosTexNorTan* data, uint32_t offset, uint32_t count)
    {
        lock_guard<mutex> lock(m_mutex);

        SP_ASSERT(offset + count <= static_cast<uint32_t>(m_vertices.size()));
        memcpy(m_vertices.data() + offset, data, count * sizeof(RHI_Vertex_PosTexNorTan));

        if (m_vertex_buffer && offset + count <= m_vertex_count_committed)
        {
            uint64_t byte_offset = static_cast<uint64_t>(offset) * sizeof(RHI_Vertex_PosTexNorTan);
            uint64_t byte_size   = static_cast<uint64_t>(count) * sizeof(RHI_Vertex_PosTexNorTan);
            m_vertex_buffer->UploadSubRegion(data, byte_offset, byte_size);
        }
    }

    void GeometryBuffer::UpdateIndices(
        const uint32_t* data,
        const uint32_t offset,
        const uint32_t count
    )
    {
        lock_guard<mutex> lock(m_mutex);

        SP_ASSERT(
            offset + count <=
            static_cast<uint32_t>(m_indices.size())
        );
        memcpy(
            m_indices.data() + offset,
            data,
            count * sizeof(uint32_t)
        );

        if (
            m_index_buffer &&
            offset + count <= m_index_count_committed
        )
        {
            const uint64_t byte_offset =
                static_cast<uint64_t>(offset) *
                sizeof(uint32_t);
            const uint64_t byte_size =
                static_cast<uint64_t>(count) *
                sizeof(uint32_t);
            m_index_buffer->UploadSubRegion(
                data,
                byte_offset,
                byte_size
            );
        }
    }

    void GeometryBuffer::UpdateMeshletBounds(
        const Sb_MeshletBounds* data,
        const uint32_t offset,
        const uint32_t count
    )
    {
        lock_guard<mutex> lock(m_mutex);

        SP_ASSERT(
            offset + count <=
            static_cast<uint32_t>(m_meshlet_bounds.size())
        );
        memcpy(
            m_meshlet_bounds.data() + offset,
            data,
            count * sizeof(Sb_MeshletBounds)
        );

        if (
            m_meshlet_bounds_buffer &&
            offset + count <= m_meshlet_bounds_count_committed
        )
        {
            const uint64_t byte_offset =
                static_cast<uint64_t>(offset) *
                sizeof(Sb_MeshletBounds);
            const uint64_t byte_size =
                static_cast<uint64_t>(count) *
                sizeof(Sb_MeshletBounds);
            m_meshlet_bounds_buffer->UploadSubRegion(
                data,
                byte_offset,
                byte_size
            );
        }
    }

    void GeometryBuffer::BuildIfDirty()
    {
        lock_guard<mutex> lock(m_mutex);

        if (!m_dirty || m_vertices.empty() || m_indices.empty())
        {
            return;
        }

        uint32_t vertex_count         = static_cast<uint32_t>(m_vertices.size());
        uint32_t index_count          = static_cast<uint32_t>(m_indices.size());
        uint32_t meshlet_bounds_count = static_cast<uint32_t>(m_meshlet_bounds.size());
        uint32_t instance_count       = static_cast<uint32_t>(m_instances.size());

        // ensure slot 0 always has an identity entry so non-instanced indirect draws read identity
        if (instance_count == 0)
        {
            m_instances.push_back(Instance::GetIdentity());
            instance_count = 1;
            m_dirty        = true;
        }

        m_was_rebuilt           = false;
        bool needs_full_rebuild = !m_vertex_buffer || !m_index_buffer || !m_meshlet_bounds_buffer || !m_instance_buffer ||
                                  vertex_count > m_vertex_capacity ||
                                  index_count > m_index_capacity ||
                                  meshlet_bounds_count > m_meshlet_bounds_capacity ||
                                  instance_count > m_instance_capacity;

        if (needs_full_rebuild)
        {
            // allocate with headroom so late-arriving meshes don't trigger another rebuild
            // a flat 25% headroom on a multi-gb instance buffer wastes hundreds of mb, clamp to 64mb of slack
            auto add_headroom = [](uint64_t count, uint64_t stride, uint64_t max_slack_bytes) -> uint32_t
            {
                uint64_t default_grown_count = static_cast<uint64_t>(static_cast<double>(count) * static_cast<double>(growth_factor));
                uint64_t default_slack_bytes = (default_grown_count - count) * stride;
                if (default_slack_bytes > max_slack_bytes)
                {
                    uint64_t capped_extra = max_slack_bytes / max<uint64_t>(stride, 1);
                    return static_cast<uint32_t>(count + capped_extra);
                }
                return static_cast<uint32_t>(default_grown_count);
            };

            constexpr uint64_t max_slack_bytes = 64ull * 1024ull * 1024ull;
            uint32_t new_vertex_capacity         = max(add_headroom(vertex_count,         sizeof(RHI_Vertex_PosTexNorTan), max_slack_bytes), m_vertex_capacity);
            uint32_t new_index_capacity          = max(add_headroom(index_count,          sizeof(uint32_t),                max_slack_bytes), m_index_capacity);
            uint32_t new_meshlet_bounds_capacity = max(add_headroom(meshlet_bounds_count, sizeof(Sb_MeshletBounds),        max_slack_bytes), max(m_meshlet_bounds_capacity, 1u));
            uint32_t new_instance_capacity       = max(add_headroom(instance_count,       sizeof(Instance),                max_slack_bytes), max(m_instance_capacity, 1u));

            // allocate into temporaries first so a failure doesn't tear down the previously-working buffers
            // the renderer keeps using the old buffers (with whatever was previously committed) until the
            // new set fully succeeds, this avoids null vkbuffer dereferences in downstream passes
            auto new_vertex_buffer = make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Vertex,
                sizeof(RHI_Vertex_PosTexNorTan),
                new_vertex_capacity,
                nullptr,
                false,
                "geometry_buffer_vertex"
            );

            auto new_index_buffer = make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Index,
                sizeof(uint32_t),
                new_index_capacity,
                nullptr,
                false,
                "geometry_buffer_index"
            );

            auto new_meshlet_bounds_buffer = make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Storage,
                sizeof(Sb_MeshletBounds),
                new_meshlet_bounds_capacity,
                nullptr,
                false,
                "geometry_buffer_meshlet_bounds"
            );

            auto new_instance_buffer = make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Instance,
                sizeof(Instance),
                new_instance_capacity,
                nullptr,
                false,
                "geometry_buffer_instances"
            );

            bool allocation_failed = !new_vertex_buffer->GetRhiResource()         ||
                                     !new_index_buffer->GetRhiResource()          ||
                                     !new_meshlet_bounds_buffer->GetRhiResource() ||
                                     !new_instance_buffer->GetRhiResource();
            if (allocation_failed)
            {
                // log once per session, the same world will hit this every time AppendInstances marks the buffer dirty during async loading and we don't need a wall of identical errors
                // Shutdown() resets the flag so the next world-load gets its own fresh log
                if (!m_oom_logged)
                {
                    SP_LOG_ERROR("Failed to allocate global geometry buffer (vertex_capacity=%u, index_capacity=%u, meshlet_capacity=%u, instance_capacity=%u), the world is too large for the available device memory, dropping further appends",
                        new_vertex_capacity, new_index_capacity, new_meshlet_bounds_capacity, new_instance_capacity);
                    m_oom_logged = true;
                }

                // record the failed instance count, future AppendInstances calls bail out so m_instances never grows past this point again
                m_instance_capacity_failed_at = instance_count;

                // truncate cpu-side instance vector back to whatever the last successful gpu commit can hold (zero on a first-build failure, identity slot 0 is reseeded on the next append)
                // without this, m_instances would stay at the failed size and the next rebuild would re-attempt the same too-big alloc
                if (m_instances.size() > m_instance_count_committed)
                {
                    m_instances.resize(m_instance_count_committed);
                }

                m_dirty       = false;
                m_was_rebuilt = false;
                return;
            }

            // upload committed data into the newly allocated buffers before swapping
            new_vertex_buffer->UploadSubRegion(m_vertices.data(), 0, vertex_count * sizeof(RHI_Vertex_PosTexNorTan));
            new_index_buffer->UploadSubRegion(m_indices.data(), 0, index_count * sizeof(uint32_t));
            if (meshlet_bounds_count > 0)
            {
                new_meshlet_bounds_buffer->UploadSubRegion(m_meshlet_bounds.data(), 0, meshlet_bounds_count * sizeof(Sb_MeshletBounds));
            }
            new_instance_buffer->UploadSubRegion(m_instances.data(), 0, instance_count * sizeof(Instance));

            // commit, the old buffers go through the deletion queue here so frames in flight finish using them
            m_vertex_buffer         = std::move(new_vertex_buffer);
            m_index_buffer          = std::move(new_index_buffer);
            m_meshlet_bounds_buffer = std::move(new_meshlet_bounds_buffer);
            m_instance_buffer       = std::move(new_instance_buffer);

            m_vertex_capacity                = new_vertex_capacity;
            m_index_capacity                 = new_index_capacity;
            m_meshlet_bounds_capacity        = new_meshlet_bounds_capacity;
            m_instance_capacity              = new_instance_capacity;

            m_vertex_count_committed         = vertex_count;
            m_index_count_committed          = index_count;
            m_meshlet_bounds_count_committed = meshlet_bounds_count;
            m_instance_count_committed       = instance_count;

            m_was_rebuilt = true;

            SP_LOG_INFO("Global geometry buffer built: %u vertices (%.2f MB), %u indices (%.2f MB), %u meshlets (%.2f MB), %u instances, capacity: %u vertices, %u indices, %u meshlets, %u instances",
                vertex_count,
                (vertex_count * sizeof(RHI_Vertex_PosTexNorTan)) / (1024.0f * 1024.0f),
                index_count,
                (index_count * sizeof(uint32_t)) / (1024.0f * 1024.0f),
                meshlet_bounds_count,
                (meshlet_bounds_count * sizeof(Sb_MeshletBounds)) / (1024.0f * 1024.0f),
                instance_count,
                m_vertex_capacity,
                m_index_capacity,
                m_meshlet_bounds_capacity,
                m_instance_capacity
            );
        }
        else
        {
            // the new data fits within the pre-allocated capacity, upload only the new portion
            uint32_t new_vertices  = vertex_count - m_vertex_count_committed;
            uint32_t new_indices   = index_count - m_index_count_committed;
            uint32_t new_meshlets  = meshlet_bounds_count - m_meshlet_bounds_count_committed;
            uint32_t new_instances = instance_count - m_instance_count_committed;

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

            if (new_meshlets > 0)
            {
                uint64_t offset = static_cast<uint64_t>(m_meshlet_bounds_count_committed) * sizeof(Sb_MeshletBounds);
                uint64_t size   = static_cast<uint64_t>(new_meshlets) * sizeof(Sb_MeshletBounds);
                m_meshlet_bounds_buffer->UploadSubRegion(m_meshlet_bounds.data() + m_meshlet_bounds_count_committed, offset, size);
            }

            if (new_instances > 0)
            {
                uint64_t offset = static_cast<uint64_t>(m_instance_count_committed) * sizeof(Instance);
                uint64_t size   = static_cast<uint64_t>(new_instances) * sizeof(Instance);
                m_instance_buffer->UploadSubRegion(m_instances.data() + m_instance_count_committed, offset, size);
            }

            m_vertex_count_committed         = vertex_count;
            m_index_count_committed          = index_count;
            m_meshlet_bounds_count_committed = meshlet_bounds_count;
            m_instance_count_committed       = instance_count;

            SP_LOG_INFO("Global geometry buffer updated: +%u vertices, +%u indices, +%u meshlets, +%u instances (sub-region upload, no rebuild)",
                new_vertices, new_indices, new_meshlets, new_instances
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

    void GeometryBuffer::Reserve(uint32_t vertex_count, uint32_t index_count, uint32_t meshlet_bounds_count, uint32_t instance_count)
    {
        lock_guard<mutex> lock(m_mutex);

        m_vertex_capacity         = max(m_vertex_capacity,         vertex_count);
        m_index_capacity          = max(m_index_capacity,          index_count);
        m_meshlet_bounds_capacity = max(m_meshlet_bounds_capacity, meshlet_bounds_count);
        m_instance_capacity       = max(m_instance_capacity,       instance_count);
    }

    void GeometryBuffer::Shutdown()
    {
        m_vertex_buffer         = nullptr;
        m_index_buffer          = nullptr;
        m_meshlet_bounds_buffer = nullptr;
        m_instance_buffer       = nullptr;
        m_vertices.clear();
        m_vertices.shrink_to_fit();
        m_indices.clear();
        m_indices.shrink_to_fit();
        m_meshlet_bounds.clear();
        m_meshlet_bounds.shrink_to_fit();
        m_instances.clear();
        m_instances.shrink_to_fit();
        m_vertex_count_committed         = 0;
        m_index_count_committed          = 0;
        m_meshlet_bounds_count_committed = 0;
        m_instance_count_committed       = 0;
        m_vertex_capacity                = 0;
        m_index_capacity                 = 0;
        m_meshlet_bounds_capacity        = 0;
        m_instance_capacity              = 0;
        m_instance_capacity_failed_at    = 0;
        m_dirty                          = false;
        m_was_rebuilt                    = false;
        m_oom_logged                     = false;
    }

    RHI_Buffer* GeometryBuffer::GetVertexBuffer()
    {
        return m_vertex_buffer.get();
    }

    RHI_Buffer* GeometryBuffer::GetIndexBuffer()
    {
        return m_index_buffer.get();
    }

    RHI_Buffer* GeometryBuffer::GetMeshletBoundsBuffer()
    {
        return m_meshlet_bounds_buffer.get();
    }

    RHI_Buffer* GeometryBuffer::GetInstanceBuffer()
    {
        return m_instance_buffer.get();
    }
}
