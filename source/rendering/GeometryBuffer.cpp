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
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Device.h"
#include <algorithm>
#include <mutex>
//===============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        // cpu-side accumulators
        // meshlet_micro_indices stays one corner per element here, the gpu copy packs four corners per uint
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;
        vector<Sb_MeshletBounds> meshlet_bounds;
        vector<uint32_t> meshlet_vertices;
        vector<uint32_t> meshlet_micro_indices;
        vector<Instance> instances;

        // micro index packing, a corner is a meshlet local vertex id below MESHLET_MAX_VERTICES so a byte is enough
        // every append pads to a multiple of this so a mesh block never straddles a uint and sub-region uploads stay aligned
        constexpr uint32_t micro_indices_per_uint = 4;

        uint32_t packed_micro_count(const uint32_t corner_count)
        {
            return (corner_count + micro_indices_per_uint - 1) / micro_indices_per_uint;
        }

        void pack_micro_indices(const uint32_t* corners, const uint32_t corner_count, vector<uint32_t>& packed_out)
        {
            const uint32_t packed_count = packed_micro_count(corner_count);
            packed_out.assign(packed_count, 0);

            for (uint32_t i = 0; i < corner_count; i++)
            {
                packed_out[i / micro_indices_per_uint] |=
                    (corners[i] & 0xFFu) << ((i % micro_indices_per_uint) * 8u);
            }
        }

        // gpu buffers
        unique_ptr<RHI_Buffer> vertex_buffer;
        unique_ptr<RHI_Buffer> index_buffer;
        unique_ptr<RHI_Buffer> meshlet_bounds_buffer;
        unique_ptr<RHI_Buffer> meshlet_vertex_buffer;
        unique_ptr<RHI_Buffer> meshlet_micro_index_buffer;
        unique_ptr<RHI_Buffer> instance_buffer;

        // element counts already uploaded to the gpu
        uint32_t vertex_count_committed              = 0;
        uint32_t index_count_committed               = 0;
        uint32_t meshlet_bounds_count_committed      = 0;
        uint32_t meshlet_vertex_count_committed      = 0;
        uint32_t meshlet_micro_count_committed       = 0;
        uint32_t instance_count_committed            = 0;

        // actual gpu buffer element counts, only written after a successful alloc
        uint32_t vertex_capacity         = 0;
        uint32_t index_capacity          = 0;
        uint32_t meshlet_bounds_capacity = 0;
        uint32_t meshlet_vertex_capacity = 0;
        uint32_t meshlet_micro_capacity  = 0;
        uint32_t instance_capacity       = 0;

        // requested floors from Reserve, must not overwrite the gpu sizes above
        uint32_t vertex_reserve         = 0;
        uint32_t index_reserve          = 0;
        uint32_t meshlet_bounds_reserve = 0;
        uint32_t meshlet_vertex_reserve = 0;
        uint32_t meshlet_micro_reserve  = 0;
        uint32_t instance_reserve       = 0;

        bool dirty       = false;
        bool was_rebuilt = false;
        mutex buffer_mutex;

        // hard-cap learned from previous oom failures, prevents retrying ever-larger allocations every frame
        // once set, AppendInstances drops new instances and BuildIfDirty stops attempting to grow past it
        uint32_t instance_capacity_failed_at = 0;

        // logs the oom error exactly once per session so async grass tile arrivals don't spam the same message
        bool oom_logged = false;

        // growth factor applied when allocating gpu buffers
        constexpr float growth_factor = 1.25f;

        // deferred vertex uploads
        //
        // UploadSubRegion on a device local buffer stages and submits an immediate copy, which
        // costs a queue submit and a wait. one skinned character per frame is fine, a crowd is not,
        // so writes only touch the cpu mirror and record a range. the frame flush coalesces them
        // into a handful of copies
        struct DirtyRange
        {
            uint32_t offset = 0;
            uint32_t count  = 0;
        };
        vector<DirtyRange> vertex_dirty_ranges;

        // two ranges closer than this merge into one, re-uploading the untouched gap is far cheaper
        // than a second submit
        constexpr uint32_t dirty_merge_slack = 4096;

        void flush_vertex_updates()
        {
            if (vertex_dirty_ranges.empty())
            {
                return;
            }

            if (!vertex_buffer)
            {
                vertex_dirty_ranges.clear();
                return;
            }

            sort(
                vertex_dirty_ranges.begin(),
                vertex_dirty_ranges.end(),
                [](const DirtyRange& a, const DirtyRange& b)
                {
                    return a.offset < b.offset;
                }
            );

            uint32_t begin = vertex_dirty_ranges[0].offset;
            uint32_t end   = begin + vertex_dirty_ranges[0].count;

            auto upload = [](const uint32_t first, const uint32_t last)
            {
                if (last <= first || last > vertex_count_committed)
                {
                    return;
                }

                const uint64_t byte_offset = static_cast<uint64_t>(first) * sizeof(RHI_Vertex_PosTexNorTan);
                const uint64_t byte_size   = static_cast<uint64_t>(last - first) * sizeof(RHI_Vertex_PosTexNorTan);
                vertex_buffer->UploadSubRegion(vertices.data() + first, byte_offset, byte_size);
            };

            for (size_t i = 1; i < vertex_dirty_ranges.size(); ++i)
            {
                const DirtyRange& range = vertex_dirty_ranges[i];
                if (range.offset <= end + dirty_merge_slack)
                {
                    end = max(end, range.offset + range.count);
                    continue;
                }

                upload(begin, end);
                begin = range.offset;
                end   = range.offset + range.count;
            }

            upload(begin, end);
            vertex_dirty_ranges.clear();
        }
    }

    uint32_t GeometryBuffer::AppendVertices(const RHI_Vertex_PosTexNorTan* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        uint32_t base_offset = static_cast<uint32_t>(vertices.size());
        vertices.insert(vertices.end(), data, data + count);
        dirty = true;

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendIndices(const uint32_t* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        uint32_t base_offset = static_cast<uint32_t>(indices.size());
        indices.insert(indices.end(), data, data + count);
        dirty = true;

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendMeshletBounds(const Sb_MeshletBounds* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        uint32_t base_offset = static_cast<uint32_t>(meshlet_bounds.size());
        if (count > 0)
        {
            meshlet_bounds.insert(meshlet_bounds.end(), data, data + count);
            dirty = true;
        }

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendMeshletVertices(const uint32_t* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        uint32_t base_offset = static_cast<uint32_t>(meshlet_vertices.size());
        if (count > 0)
        {
            meshlet_vertices.insert(meshlet_vertices.end(), data, data + count);
            dirty = true;
        }

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendMeshletMicroIndices(const uint32_t* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        uint32_t base_offset = static_cast<uint32_t>(meshlet_micro_indices.size());
        if (count > 0)
        {
            meshlet_micro_indices.insert(meshlet_micro_indices.end(), data, data + count);

            // pad so the next block starts on a uint boundary, the padding corners are never indexed by a meshlet
            const size_t remainder = meshlet_micro_indices.size() % micro_indices_per_uint;
            if (remainder != 0)
            {
                meshlet_micro_indices.resize(meshlet_micro_indices.size() + (micro_indices_per_uint - remainder), 0u);
            }

            dirty = true;
        }

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendInstances(const Instance* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        // seed slot 0 with an identity instance so non-instanced draws can read identity at offset 0
        if (instances.empty())
        {
            instances.push_back(Instance::GetIdentity());
            dirty = true;
        }

        uint32_t base_offset = static_cast<uint32_t>(instances.size());

        // once an instance oom has been seen, refuse all further appends so the cpu vector never grows past the last gpu-resident size
        // also avoids the per-frame rebuild + log spam pattern (every async grass tile arrival would otherwise retry an alloc that already failed)
        if (instance_capacity_failed_at != 0)
        {
            return base_offset;
        }

        if (count > 0)
        {
            instances.insert(instances.end(), data, data + count);
            dirty = true;
        }

        return base_offset;
    }

    void GeometryBuffer::UpdateVertices(const RHI_Vertex_PosTexNorTan* data, uint32_t offset, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        SP_ASSERT(offset + count <= static_cast<uint32_t>(vertices.size()));
        memcpy(vertices.data() + offset, data, count * sizeof(RHI_Vertex_PosTexNorTan));

        // queued, the frame flush coalesces every deformable mesh into a few copies
        if (count > 0 && vertex_buffer && offset + count <= vertex_count_committed)
        {
            vertex_dirty_ranges.push_back({ offset, count });
        }
    }

    void GeometryBuffer::UpdateIndices(const uint32_t* data, const uint32_t offset, const uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        SP_ASSERT(offset + count <= static_cast<uint32_t>(indices.size()));
        memcpy(indices.data() + offset, data, count * sizeof(uint32_t));

        if (index_buffer && offset + count <= index_count_committed)
        {
            const uint64_t byte_offset = static_cast<uint64_t>(offset) * sizeof(uint32_t);
            const uint64_t byte_size   = static_cast<uint64_t>(count) * sizeof(uint32_t);
            index_buffer->UploadSubRegion(data, byte_offset, byte_size);
        }
    }

    void GeometryBuffer::UpdateMeshletBounds(const Sb_MeshletBounds* data, const uint32_t offset, const uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        SP_ASSERT(offset + count <= static_cast<uint32_t>(meshlet_bounds.size()));
        memcpy(meshlet_bounds.data() + offset, data, count * sizeof(Sb_MeshletBounds));

        if (meshlet_bounds_buffer && offset + count <= meshlet_bounds_count_committed)
        {
            const uint64_t byte_offset = static_cast<uint64_t>(offset) * sizeof(Sb_MeshletBounds);
            const uint64_t byte_size   = static_cast<uint64_t>(count) * sizeof(Sb_MeshletBounds);
            meshlet_bounds_buffer->UploadSubRegion(data, byte_offset, byte_size);
        }
    }

    void GeometryBuffer::UpdateMeshletVertices(const uint32_t* data, const uint32_t offset, const uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        SP_ASSERT(offset + count <= static_cast<uint32_t>(meshlet_vertices.size()));
        memcpy(meshlet_vertices.data() + offset, data, count * sizeof(uint32_t));

        if (meshlet_vertex_buffer && offset + count <= meshlet_vertex_count_committed)
        {
            const uint64_t byte_offset = static_cast<uint64_t>(offset) * sizeof(uint32_t);
            const uint64_t byte_size   = static_cast<uint64_t>(count) * sizeof(uint32_t);
            meshlet_vertex_buffer->UploadSubRegion(data, byte_offset, byte_size);
        }
    }

    void GeometryBuffer::UpdateMeshletMicroIndices(const uint32_t* data, const uint32_t offset, const uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        SP_ASSERT(offset + count <= static_cast<uint32_t>(meshlet_micro_indices.size()));
        memcpy(meshlet_micro_indices.data() + offset, data, count * sizeof(uint32_t));

        if (count > 0 && meshlet_micro_index_buffer && offset + count <= meshlet_micro_count_committed)
        {
            // repack from the cpu array rather than from data, so the tail uint keeps the block padding that lives past count
            const uint32_t aligned_offset = (offset / micro_indices_per_uint) * micro_indices_per_uint;
            const uint32_t aligned_count  = packed_micro_count(offset + count - aligned_offset) * micro_indices_per_uint;
            const uint32_t clamped_count  = min(aligned_count, static_cast<uint32_t>(meshlet_micro_indices.size()) - aligned_offset);

            vector<uint32_t> packed;
            pack_micro_indices(meshlet_micro_indices.data() + aligned_offset, clamped_count, packed);

            const uint64_t byte_offset = static_cast<uint64_t>(packed_micro_count(aligned_offset)) * sizeof(uint32_t);
            const uint64_t byte_size   = static_cast<uint64_t>(packed.size()) * sizeof(uint32_t);
            meshlet_micro_index_buffer->UploadSubRegion(packed.data(), byte_offset, byte_size);
        }
    }

    void GeometryBuffer::BuildIfDirty()
    {
        lock_guard<mutex> lock(buffer_mutex);

        // once per frame sync point for skinning and other deformables, must run before the dirty
        // early out because deformable writes do not mark the buffer dirty
        flush_vertex_updates();

        if (!dirty || vertices.empty() || indices.empty())
        {
            return;
        }

        uint32_t vertex_count         = static_cast<uint32_t>(vertices.size());
        uint32_t index_count          = static_cast<uint32_t>(indices.size());
        uint32_t meshlet_bounds_count = static_cast<uint32_t>(meshlet_bounds.size());
        uint32_t meshlet_vertex_count = static_cast<uint32_t>(meshlet_vertices.size());
        uint32_t meshlet_micro_count  = static_cast<uint32_t>(meshlet_micro_indices.size());
        uint32_t instance_count       = static_cast<uint32_t>(instances.size());

        // ensure slot 0 always has an identity entry so non-instanced indirect draws read identity
        if (instance_count == 0)
        {
            instances.push_back(Instance::GetIdentity());
            instance_count = 1;
            dirty          = true;
        }

        was_rebuilt             = false;
        bool needs_full_rebuild = !vertex_buffer || !index_buffer || !meshlet_bounds_buffer ||
                                  !meshlet_vertex_buffer || !meshlet_micro_index_buffer || !instance_buffer ||
                                  vertex_count > vertex_capacity ||
                                  index_count > index_capacity ||
                                  meshlet_bounds_count > meshlet_bounds_capacity ||
                                  meshlet_vertex_count > meshlet_vertex_capacity ||
                                  meshlet_micro_count > meshlet_micro_capacity ||
                                  instance_count > instance_capacity ||
                                  vertex_reserve > vertex_capacity ||
                                  index_reserve > index_capacity ||
                                  meshlet_bounds_reserve > meshlet_bounds_capacity ||
                                  meshlet_vertex_reserve > meshlet_vertex_capacity ||
                                  meshlet_micro_reserve > meshlet_micro_capacity ||
                                  instance_reserve > instance_capacity;

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
            uint32_t new_vertex_capacity         = max(add_headroom(max(vertex_count, vertex_reserve), sizeof(RHI_Vertex_PosTexNorTan), max_slack_bytes), vertex_capacity);
            uint32_t new_index_capacity          = max(add_headroom(max(index_count, index_reserve), sizeof(uint32_t), max_slack_bytes), index_capacity);
            uint32_t new_meshlet_bounds_capacity = max(add_headroom(max(meshlet_bounds_count, meshlet_bounds_reserve), sizeof(Sb_MeshletBounds), max_slack_bytes), max(meshlet_bounds_capacity, 1u));
            uint32_t new_meshlet_vertex_capacity = max(add_headroom(max(max(meshlet_vertex_count, meshlet_vertex_reserve), 1u), sizeof(uint32_t), max_slack_bytes), max(meshlet_vertex_capacity, 1u));
            uint32_t new_meshlet_micro_capacity  = max(add_headroom(max(max(meshlet_micro_count, meshlet_micro_reserve), 1u), sizeof(uint32_t), max_slack_bytes), max(meshlet_micro_capacity, 1u));
            uint32_t new_instance_capacity       = max(add_headroom(max(instance_count, instance_reserve), sizeof(Instance), max_slack_bytes), max(instance_capacity, 1u));

            // allocate into temporaries so a failure leaves the previously working buffers in place
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

            auto new_meshlet_vertex_buffer = make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Storage,
                sizeof(uint32_t),
                new_meshlet_vertex_capacity,
                nullptr,
                false,
                "geometry_buffer_meshlet_vertices"
            );

            auto new_meshlet_micro_index_buffer = make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Storage,
                sizeof(uint32_t),
                packed_micro_count(new_meshlet_micro_capacity),
                nullptr,
                false,
                "geometry_buffer_meshlet_micro_indices"
            );

            auto new_instance_buffer = make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Instance,
                sizeof(Instance),
                new_instance_capacity,
                nullptr,
                false,
                "geometry_buffer_instances"
            );

            bool allocation_failed = !new_vertex_buffer->GetRhiResource()              ||
                                     !new_index_buffer->GetRhiResource()               ||
                                     !new_meshlet_bounds_buffer->GetRhiResource()      ||
                                     !new_meshlet_vertex_buffer->GetRhiResource()      ||
                                     !new_meshlet_micro_index_buffer->GetRhiResource() ||
                                     !new_instance_buffer->GetRhiResource();
            if (allocation_failed)
            {
                // log once per session, the same world will hit this every time AppendInstances marks the buffer dirty during async loading and we don't need a wall of identical errors
                // Shutdown() resets the flag so the next world-load gets its own fresh log
                if (!oom_logged)
                {
                    SP_LOG_ERROR("Failed to allocate global geometry buffer (vertex_capacity=%u, index_capacity=%u, meshlet_capacity=%u, meshlet_vertex_capacity=%u, meshlet_micro_capacity=%u, instance_capacity=%u), the world is too large for the available device memory, dropping further appends",
                        new_vertex_capacity, new_index_capacity, new_meshlet_bounds_capacity, new_meshlet_vertex_capacity, new_meshlet_micro_capacity, new_instance_capacity);
                    oom_logged = true;
                }

                // record the failed instance count, future AppendInstances calls bail out so instances never grows past this point again
                instance_capacity_failed_at = instance_count;

                // truncate cpu-side instance vector back to whatever the last successful gpu commit can hold (zero on a first-build failure, identity slot 0 is reseeded on the next append)
                // without this, instances would stay at the failed size and the next rebuild would re-attempt the same too-big alloc
                if (instances.size() > instance_count_committed)
                {
                    instances.resize(instance_count_committed);
                }

                dirty       = false;
                was_rebuilt = false;
                return;
            }

            // upload committed data into the newly allocated buffers before swapping
            new_vertex_buffer->UploadSubRegion(vertices.data(), 0, vertex_count * sizeof(RHI_Vertex_PosTexNorTan));
            new_index_buffer->UploadSubRegion(indices.data(), 0, index_count * sizeof(uint32_t));
            if (meshlet_bounds_count > 0)
            {
                new_meshlet_bounds_buffer->UploadSubRegion(meshlet_bounds.data(), 0, meshlet_bounds_count * sizeof(Sb_MeshletBounds));
            }
            if (meshlet_vertex_count > 0)
            {
                new_meshlet_vertex_buffer->UploadSubRegion(meshlet_vertices.data(), 0, meshlet_vertex_count * sizeof(uint32_t));
            }
            if (meshlet_micro_count > 0)
            {
                vector<uint32_t> packed;
                pack_micro_indices(meshlet_micro_indices.data(), meshlet_micro_count, packed);
                new_meshlet_micro_index_buffer->UploadSubRegion(packed.data(), 0, packed.size() * sizeof(uint32_t));
            }
            new_instance_buffer->UploadSubRegion(instances.data(), 0, instance_count * sizeof(Instance));

            // commit, the old buffers go through the deletion queue here so frames in flight finish using them
            vertex_buffer              = std::move(new_vertex_buffer);
            index_buffer               = std::move(new_index_buffer);
            meshlet_bounds_buffer      = std::move(new_meshlet_bounds_buffer);
            meshlet_vertex_buffer      = std::move(new_meshlet_vertex_buffer);
            meshlet_micro_index_buffer = std::move(new_meshlet_micro_index_buffer);
            instance_buffer            = std::move(new_instance_buffer);

            vertex_capacity         = new_vertex_capacity;
            index_capacity          = new_index_capacity;
            meshlet_bounds_capacity = new_meshlet_bounds_capacity;
            meshlet_vertex_capacity = new_meshlet_vertex_capacity;
            meshlet_micro_capacity  = new_meshlet_micro_capacity;
            instance_capacity       = new_instance_capacity;

            vertex_count_committed         = vertex_count;
            index_count_committed          = index_count;
            meshlet_bounds_count_committed = meshlet_bounds_count;
            meshlet_vertex_count_committed = meshlet_vertex_count;
            meshlet_micro_count_committed  = meshlet_micro_count;
            instance_count_committed       = instance_count;

            was_rebuilt = true;

            SP_LOG_INFO("Global geometry buffer built: %u vertices (%.2f MB), %u indices (%.2f MB), %u meshlets (%.2f MB), %u meshlet verts, %u micro indices, %u instances, capacity: %u/%u/%u/%u/%u/%u",
                vertex_count,
                (vertex_count * sizeof(RHI_Vertex_PosTexNorTan)) / (1024.0f * 1024.0f),
                index_count,
                (index_count * sizeof(uint32_t)) / (1024.0f * 1024.0f),
                meshlet_bounds_count,
                (meshlet_bounds_count * sizeof(Sb_MeshletBounds)) / (1024.0f * 1024.0f),
                meshlet_vertex_count,
                meshlet_micro_count,
                instance_count,
                vertex_capacity,
                index_capacity,
                meshlet_bounds_capacity,
                meshlet_vertex_capacity,
                meshlet_micro_capacity,
                instance_capacity
            );
        }
        else
        {
            // the new data fits within the pre-allocated capacity, upload only the new portion
            uint32_t new_vertices        = vertex_count - vertex_count_committed;
            uint32_t new_indices         = index_count - index_count_committed;
            uint32_t new_meshlets        = meshlet_bounds_count - meshlet_bounds_count_committed;
            uint32_t new_meshlet_verts   = meshlet_vertex_count - meshlet_vertex_count_committed;
            uint32_t new_meshlet_micros  = meshlet_micro_count - meshlet_micro_count_committed;
            uint32_t new_instances       = instance_count - instance_count_committed;

            if (new_vertices > 0)
            {
                uint64_t offset = static_cast<uint64_t>(vertex_count_committed) * sizeof(RHI_Vertex_PosTexNorTan);
                uint64_t size   = static_cast<uint64_t>(new_vertices) * sizeof(RHI_Vertex_PosTexNorTan);
                vertex_buffer->UploadSubRegion(vertices.data() + vertex_count_committed, offset, size);
            }

            if (new_indices > 0)
            {
                uint64_t offset = static_cast<uint64_t>(index_count_committed) * sizeof(uint32_t);
                uint64_t size   = static_cast<uint64_t>(new_indices) * sizeof(uint32_t);
                index_buffer->UploadSubRegion(indices.data() + index_count_committed, offset, size);
            }

            if (new_meshlets > 0)
            {
                uint64_t offset = static_cast<uint64_t>(meshlet_bounds_count_committed) * sizeof(Sb_MeshletBounds);
                uint64_t size   = static_cast<uint64_t>(new_meshlets) * sizeof(Sb_MeshletBounds);
                meshlet_bounds_buffer->UploadSubRegion(meshlet_bounds.data() + meshlet_bounds_count_committed, offset, size);
            }

            if (new_meshlet_verts > 0)
            {
                uint64_t offset = static_cast<uint64_t>(meshlet_vertex_count_committed) * sizeof(uint32_t);
                uint64_t size   = static_cast<uint64_t>(new_meshlet_verts) * sizeof(uint32_t);
                meshlet_vertex_buffer->UploadSubRegion(meshlet_vertices.data() + meshlet_vertex_count_committed, offset, size);
            }

            if (new_meshlet_micros > 0)
            {
                // the committed count is always a multiple of the pack width, appends pad to keep it there
                vector<uint32_t> packed;
                pack_micro_indices(meshlet_micro_indices.data() + meshlet_micro_count_committed, new_meshlet_micros, packed);

                uint64_t offset = static_cast<uint64_t>(packed_micro_count(meshlet_micro_count_committed)) * sizeof(uint32_t);
                uint64_t size   = static_cast<uint64_t>(packed.size()) * sizeof(uint32_t);
                meshlet_micro_index_buffer->UploadSubRegion(packed.data(), offset, size);
            }

            if (new_instances > 0)
            {
                uint64_t offset = static_cast<uint64_t>(instance_count_committed) * sizeof(Instance);
                uint64_t size   = static_cast<uint64_t>(new_instances) * sizeof(Instance);
                instance_buffer->UploadSubRegion(instances.data() + instance_count_committed, offset, size);
            }

            vertex_count_committed         = vertex_count;
            index_count_committed          = index_count;
            meshlet_bounds_count_committed = meshlet_bounds_count;
            meshlet_vertex_count_committed = meshlet_vertex_count;
            meshlet_micro_count_committed  = meshlet_micro_count;
            instance_count_committed       = instance_count;
        }

        dirty = false;
    }

    bool GeometryBuffer::WasRebuilt()
    {
        bool result = was_rebuilt;
        was_rebuilt = false;
        return result;
    }

    void GeometryBuffer::Reserve(
        uint32_t vertex_count,
        uint32_t index_count,
        uint32_t meshlet_bounds_count,
        uint32_t meshlet_vertex_count,
        uint32_t meshlet_micro_count,
        uint32_t instance_count
    )
    {
        lock_guard<mutex> lock(buffer_mutex);

        vertex_reserve         = max(vertex_reserve,         vertex_count);
        index_reserve          = max(index_reserve,          index_count);
        meshlet_bounds_reserve = max(meshlet_bounds_reserve, meshlet_bounds_count);
        meshlet_vertex_reserve = max(meshlet_vertex_reserve, meshlet_vertex_count);
        meshlet_micro_reserve  = max(meshlet_micro_reserve,  meshlet_micro_count);
        instance_reserve       = max(instance_reserve,       instance_count);
    }

    void GeometryBuffer::Shutdown()
    {
        vertex_buffer              = nullptr;
        index_buffer               = nullptr;
        meshlet_bounds_buffer      = nullptr;
        meshlet_vertex_buffer      = nullptr;
        meshlet_micro_index_buffer = nullptr;
        instance_buffer            = nullptr;
        vertices.clear();
        vertices.shrink_to_fit();
        indices.clear();
        indices.shrink_to_fit();
        meshlet_bounds.clear();
        meshlet_bounds.shrink_to_fit();
        meshlet_vertices.clear();
        meshlet_vertices.shrink_to_fit();
        meshlet_micro_indices.clear();
        meshlet_micro_indices.shrink_to_fit();
        instances.clear();
        instances.shrink_to_fit();
        vertex_dirty_ranges.clear();
        vertex_dirty_ranges.shrink_to_fit();
        vertex_count_committed         = 0;
        index_count_committed          = 0;
        meshlet_bounds_count_committed = 0;
        meshlet_vertex_count_committed = 0;
        meshlet_micro_count_committed  = 0;
        instance_count_committed       = 0;
        vertex_capacity                = 0;
        index_capacity                 = 0;
        meshlet_bounds_capacity        = 0;
        meshlet_vertex_capacity        = 0;
        meshlet_micro_capacity         = 0;
        instance_capacity              = 0;
        vertex_reserve                 = 0;
        index_reserve                  = 0;
        meshlet_bounds_reserve         = 0;
        meshlet_vertex_reserve         = 0;
        meshlet_micro_reserve          = 0;
        instance_reserve               = 0;
        instance_capacity_failed_at    = 0;
        dirty                          = false;
        was_rebuilt                    = false;
        oom_logged                     = false;
    }

    RHI_Buffer* GeometryBuffer::GetVertexBuffer()
    {
        return vertex_buffer.get();
    }

    RHI_Buffer* GeometryBuffer::GetIndexBuffer()
    {
        return index_buffer.get();
    }

    RHI_Buffer* GeometryBuffer::GetMeshletBoundsBuffer()
    {
        return meshlet_bounds_buffer.get();
    }

    RHI_Buffer* GeometryBuffer::GetMeshletVertexBuffer()
    {
        return meshlet_vertex_buffer.get();
    }

    RHI_Buffer* GeometryBuffer::GetMeshletMicroIndexBuffer()
    {
        return meshlet_micro_index_buffer.get();
    }

    RHI_Buffer* GeometryBuffer::GetInstanceBuffer()
    {
        return instance_buffer.get();
    }
}
