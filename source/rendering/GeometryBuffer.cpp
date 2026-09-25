/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ====================
#include "pch.h"
#include "../core/Stopwatch.h"
#include "GeometryBuffer.h"
#include "Renderer.h"
#include <map>
#include "../geometry/GeneratedCache.h"
#include "../rhi/RHI_CommandList.h"
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
        // the gpu buffers are the only full copy of the world geometry, the cpu keeps just the tail
        // appended since the last upload, meshes own their source data so a full mirror would double it
        template<typename T>
        struct Stream
        {
            vector<T> tail;         // elements [committed, size)
            uint32_t committed = 0; // elements already resident on the gpu

            uint32_t size() const
            {
                return committed + static_cast<uint32_t>(tail.size());
            }

            void release_tail()
            {
                // a world load pushes gigabytes through the tail, don't keep that capacity around
                constexpr size_t keep_bytes = 4ull * 1024 * 1024;
                if (tail.capacity() * sizeof(T) > keep_bytes)
                {
                    vector<T>().swap(tail);
                }
                else
                {
                    tail.clear();
                }
            }

            void reset()
            {
                vector<T>().swap(tail);
                committed = 0;
            }
        };

        // Micro-indices are meshlet-local byte values on both CPU and GPU.
        // Keep their existing corner offsets and four-byte block alignment.
        Stream<RHI_Vertex_PosTexNorTan> vertices;
        Stream<uint32_t> indices;
        Stream<Sb_MeshletBounds> meshlet_bounds;
        Stream<uint32_t> meshlet_vertices;
        Stream<uint8_t> meshlet_micro_indices;
        Stream<Instance> instances;

        // micro index packing, a corner is a meshlet local vertex id below MESHLET_MAX_VERTICES so a byte is enough
        // every append pads to a multiple of this so a mesh block never straddles a uint and sub-region uploads stay aligned
        constexpr uint32_t micro_indices_per_uint = 4;

        uint32_t packed_micro_count(const uint32_t corner_count)
        {
            return (corner_count + micro_indices_per_uint - 1) / micro_indices_per_uint;
        }

        // gpu buffers
        unique_ptr<RHI_Buffer> vertex_buffer;
        unique_ptr<RHI_Buffer> index_buffer;
        unique_ptr<RHI_Buffer> meshlet_bounds_buffer;
        unique_ptr<RHI_Buffer> meshlet_vertex_buffer;
        unique_ptr<RHI_Buffer> meshlet_micro_index_buffer;
        unique_ptr<RHI_Buffer> instance_buffer;

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

        void upload(RHI_Buffer* buffer, const void* data, uint64_t offset, uint64_t size)
        {
            const Stopwatch timer;
            if (RHI_Device::IsRecording())
                RHI_CommandList::UpdateBuffer(buffer, offset, size, data, false);
            else
                buffer->UploadSubRegion(data, offset, size);
            if (size >= 64ull * 1024 * 1024)
            {
                const std::string name = buffer->GetObjectName();
                SP_LOG_INFO("Geometry upload '%s': %.1f MB, %.2f ms", name.c_str(),
                    size / (1024.0 * 1024.0), timer.GetElapsedTimeMs());
            }
        }

        // deferred uploads for ranges already on the gpu
        //
        // UploadSubRegion on a device local buffer stages and submits an immediate copy, which
        // costs a queue submit and a wait. one skinned character per frame is fine, a crowd is not,
        // so writes are staged per range and the frame flush merges adjacent ones into a handful of copies
        // keyed by first element, rewriting the same range within a frame replaces the staged copy
        template<typename T>
        using StagedWrites = map<uint32_t, vector<T>>;
        StagedWrites<RHI_Vertex_PosTexNorTan> vertex_writes;
        StagedWrites<Instance> instance_writes;

        // Only animated ranges get history storage; static geometry keeps its compact layout.
        struct VertexHistory
        {
            uint32_t offset = 0;
            uint32_t count = 0;
            uint64_t frame = UINT64_MAX;
            vector<RHI_Vertex_PosTexNorTan> pose; // last pose written to the source range, next frame's previous pose
        };
        map<uint32_t, VertexHistory> vertex_history;

        // writes into whichever side holds the range, the gpu part is staged when staged is given, uploaded right away otherwise
        template<typename T>
        void write(Stream<T>& stream, StagedWrites<T>* staged, RHI_Buffer* buffer, const T* data, uint32_t offset, uint32_t count)
        {
            if (count == 0)
            {
                return;
            }

            const uint32_t end = offset + count;
            if (offset < stream.committed)
            {
                const uint32_t gpu_count = min(end, stream.committed) - offset;
                if (staged)
                {
                    (*staged)[offset].assign(data, data + gpu_count);
                }
                else if (buffer)
                {
                    buffer->UploadSubRegion(data, static_cast<uint64_t>(offset) * sizeof(T), static_cast<uint64_t>(gpu_count) * sizeof(T));
                }
            }

            if (end > stream.committed)
            {
                const uint32_t first = max(offset, stream.committed);
                memcpy(stream.tail.data() + (first - stream.committed), data + (first - offset), static_cast<size_t>(end - first) * sizeof(T));
            }
        }

        template<typename T>
        void flush_staged(StagedWrites<T>& staged, RHI_Buffer* buffer, const uint32_t committed)
        {
            if (staged.empty())
            {
                return;
            }

            if (!buffer)
            {
                staged.clear();
                return;
            }

            vector<T> merged;
            uint32_t merged_offset = 0;
            auto submit = [&]()
            {
                if (!merged.empty())
                {
                    upload(buffer, merged.data(), static_cast<uint64_t>(merged_offset) * sizeof(T), merged.size() * sizeof(T));
                    merged.clear();
                }
            };

            for (auto& [offset, data] : staged)
            {
                if (offset + data.size() > committed)
                {
                    continue;
                }

                if (!merged.empty() && offset == merged_offset + merged.size())
                {
                    merged.insert(merged.end(), data.begin(), data.end());
                    continue;
                }

                submit();
                merged_offset = offset;
                merged        = std::move(data);
            }
            submit();
            staged.clear();
        }

        template<typename T>
        void upload_tail(Stream<T>& stream, RHI_Buffer* buffer)
        {
            if (!stream.tail.empty())
            {
                const uint64_t offset = static_cast<uint64_t>(stream.committed) * sizeof(T);
                upload(buffer, stream.tail.data(), offset, stream.tail.size() * sizeof(T));
            }
            stream.committed = stream.size();
            stream.release_tail();
        }

        // growth keeps offsets and contents, the committed range moves gpu side so the cpu never needs it back
        void adopt(unique_ptr<RHI_Buffer>& current, unique_ptr<RHI_Buffer>& replacement, uint64_t committed_bytes)
        {
            if (current && committed_bytes > 0)
            {
                RHI_CommandList::CopyBufferContents(current.get(), replacement.get(), committed_bytes);
            }
            current = std::move(replacement);
        }
    }

    uint32_t GeometryBuffer::AppendVertices(const RHI_Vertex_PosTexNorTan* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        uint32_t base_offset = vertices.size();
        vertices.tail.insert(vertices.tail.end(), data, data + count);
        dirty = true;

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendIndices(const uint32_t* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        uint32_t base_offset = indices.size();
        indices.tail.insert(indices.tail.end(), data, data + count);
        dirty = true;

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendMeshletBounds(const Sb_MeshletBounds* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        uint32_t base_offset = meshlet_bounds.size();
        if (count > 0)
        {
            meshlet_bounds.tail.insert(meshlet_bounds.tail.end(), data, data + count);
            dirty = true;
        }

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendMeshletVertices(const uint32_t* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        uint32_t base_offset = meshlet_vertices.size();
        if (count > 0)
        {
            meshlet_vertices.tail.insert(meshlet_vertices.tail.end(), data, data + count);
            dirty = true;
        }

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendMeshletMicroIndices(const uint32_t* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        uint32_t base_offset = meshlet_micro_indices.size();
        if (count > 0)
        {
            vector<uint8_t>& tail   = meshlet_micro_indices.tail;
            const size_t tail_start = tail.size();
            tail.resize(tail_start + count);
            for (uint32_t i = 0; i < count; ++i)
                tail[tail_start + i] = static_cast<uint8_t>(data[i]);

            // pad so the next block starts on a uint boundary, the padding corners are never indexed by a meshlet
            // committed always ends on a boundary, so aligning the tail aligns the whole stream
            const size_t remainder = tail.size() % micro_indices_per_uint;
            if (remainder != 0)
            {
                tail.resize(tail.size() + (micro_indices_per_uint - remainder), 0u);
            }

            dirty = true;
        }

        return base_offset;
    }

    uint32_t GeometryBuffer::AppendInstances(const Instance* data, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        // seed slot 0 with an identity instance so non-instanced draws can read identity at offset 0
        if (instances.size() == 0)
        {
            instances.tail.push_back(Instance::GetIdentity());
            dirty = true;
        }

        uint32_t base_offset = instances.size();

        // once an instance oom has been seen, refuse all further appends so the cpu vector never grows past the last gpu-resident size
        // also avoids the per-frame rebuild + log spam pattern (every async grass tile arrival would otherwise retry an alloc that already failed)
        if (instance_capacity_failed_at != 0)
        {
            return base_offset;
        }

        if (count > 0)
        {
            instances.tail.insert(instances.tail.end(), data, data + count);
            dirty = true;
        }

        return base_offset;
    }

    bool GeometryBuffer::UpdateInstances(const Instance* data, uint32_t offset, uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        // slot 0 is the shared identity, never a writable range
        if (offset == 0 || count == 0 || offset + count > instances.size())
        {
            return false;
        }

        write(instances, &instance_writes, instance_buffer.get(), data, offset, count);
        return true;
    }

    void GeometryBuffer::UpdateVertices(const RHI_Vertex_PosTexNorTan* data, uint32_t offset, uint32_t count, bool track_motion)
    {
        lock_guard<mutex> lock(buffer_mutex);

        SP_ASSERT(offset + count <= vertices.size());
        if (track_motion && count > 0)
        {
            VertexHistory& history = vertex_history[offset];
            if (history.count != count)
            {
                history.offset = vertices.size();
                history.count = count;
                history.frame = UINT64_MAX;
                history.pose.clear();
                vertices.tail.resize(vertices.tail.size() + count);
                dirty = true;
            }

            // Multiple animation/IK updates before a render must retain the last displayed pose.
            const uint64_t frame = Renderer::GetFrameNumber();
            if (history.frame != frame)
            {
                const RHI_Vertex_PosTexNorTan* previous = history.pose.size() == count ? history.pose.data() : data;
                write(vertices, &vertex_writes, vertex_buffer.get(), previous, history.offset, count);
                history.frame = frame;
            }
            history.pose.assign(data, data + count);
        }

        // queued, the frame flush coalesces every deformable mesh into a few copies
        write(vertices, &vertex_writes, vertex_buffer.get(), data, offset, count);
    }

    uint32_t GeometryBuffer::GetPreviousVertexOffset(uint32_t offset)
    {
        lock_guard<mutex> lock(buffer_mutex);
        auto it = vertex_history.upper_bound(offset);
        if (it == vertex_history.begin()) return 0;
        --it;
        const VertexHistory& history = it->second;
        // A paused or throttled animator must not replay its last motion vector.
        if (offset - it->first >= history.count || history.frame != Renderer::GetFrameNumber()) return 0;
        return history.offset - it->first;
    }

    void GeometryBuffer::UpdateIndices(const uint32_t* data, const uint32_t offset, const uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        SP_ASSERT(offset + count <= indices.size());
        write(indices, static_cast<StagedWrites<uint32_t>*>(nullptr), index_buffer.get(), data, offset, count);
    }

    void GeometryBuffer::UpdateMeshletBounds(const Sb_MeshletBounds* data, const uint32_t offset, const uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        SP_ASSERT(offset + count <= meshlet_bounds.size());
        write(meshlet_bounds, static_cast<StagedWrites<Sb_MeshletBounds>*>(nullptr), meshlet_bounds_buffer.get(), data, offset, count);
    }

    void GeometryBuffer::UpdateMeshletVertices(const uint32_t* data, const uint32_t offset, const uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        SP_ASSERT(offset + count <= meshlet_vertices.size());
        write(meshlet_vertices, static_cast<StagedWrites<uint32_t>*>(nullptr), meshlet_vertex_buffer.get(), data, offset, count);
    }

    void GeometryBuffer::UpdateMeshletMicroIndices(const uint32_t* data, const uint32_t offset, const uint32_t count)
    {
        lock_guard<mutex> lock(buffer_mutex);

        SP_ASSERT(offset + count <= meshlet_micro_indices.size());
        if (count == 0)
        {
            return;
        }

        // callers rewrite whole mesh blocks, which start on a uint and own the zero padding up to the next one
        SP_ASSERT(offset % micro_indices_per_uint == 0);
        const uint32_t padded_count = min(packed_micro_count(count) * micro_indices_per_uint, meshlet_micro_indices.size() - offset);
        vector<uint8_t> bytes(padded_count, 0u);
        for (uint32_t i = 0; i < count; ++i)
            bytes[i] = static_cast<uint8_t>(data[i]);

        write(meshlet_micro_indices, static_cast<StagedWrites<uint8_t>*>(nullptr), meshlet_micro_index_buffer.get(), bytes.data(), offset, padded_count);
    }

    void GeometryBuffer::BuildIfDirty()
    {
        lock_guard<mutex> lock(buffer_mutex);

        // once per frame sync point for skinning and other deformables, must run before the dirty
        // early out because deformable writes do not mark the buffer dirty
        flush_staged(vertex_writes, vertex_buffer.get(), vertices.committed);
        flush_staged(instance_writes, instance_buffer.get(), instances.committed);

        if (!dirty || vertices.size() == 0 || indices.size() == 0)
        {
            return;
        }

        // ensure slot 0 always has an identity entry so non-instanced indirect draws read identity
        if (instances.size() == 0)
        {
            instances.tail.push_back(Instance::GetIdentity());
            dirty = true;
        }

        uint32_t vertex_count         = vertices.size();
        uint32_t index_count          = indices.size();
        uint32_t meshlet_bounds_count = meshlet_bounds.size();
        uint32_t meshlet_vertex_count = meshlet_vertices.size();
        uint32_t meshlet_micro_count  = meshlet_micro_indices.size();
        uint32_t instance_count       = instances.size();

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
            auto new_vertex_buffer = (!vertex_buffer || max(vertex_count, vertex_reserve) > vertex_capacity) ? make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Vertex,
                sizeof(RHI_Vertex_PosTexNorTan),
                new_vertex_capacity,
                nullptr,
                false,
                "geometry_buffer_vertex"
            ) : nullptr;

            auto new_index_buffer = (!index_buffer || max(index_count, index_reserve) > index_capacity) ? make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Index,
                sizeof(uint32_t),
                new_index_capacity,
                nullptr,
                false,
                "geometry_buffer_index"
            ) : nullptr;

            auto new_meshlet_bounds_buffer = (!meshlet_bounds_buffer || max(meshlet_bounds_count, meshlet_bounds_reserve) > meshlet_bounds_capacity) ? make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Storage,
                sizeof(Sb_MeshletBounds),
                new_meshlet_bounds_capacity,
                nullptr,
                false,
                "geometry_buffer_meshlet_bounds"
            ) : nullptr;

            auto new_meshlet_vertex_buffer = (!meshlet_vertex_buffer || max(meshlet_vertex_count, meshlet_vertex_reserve) > meshlet_vertex_capacity) ? make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Storage,
                sizeof(uint32_t),
                new_meshlet_vertex_capacity,
                nullptr,
                false,
                "geometry_buffer_meshlet_vertices"
            ) : nullptr;

            auto new_meshlet_micro_index_buffer = (!meshlet_micro_index_buffer || max(meshlet_micro_count, meshlet_micro_reserve) > meshlet_micro_capacity) ? make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Storage,
                sizeof(uint32_t),
                packed_micro_count(new_meshlet_micro_capacity),
                nullptr,
                false,
                "geometry_buffer_meshlet_micro_indices"
            ) : nullptr;

            auto new_instance_buffer = (!instance_buffer || max(instance_count, instance_reserve) > instance_capacity) ? make_unique<RHI_Buffer>(
                RHI_Buffer_Type::Instance,
                sizeof(Instance),
                new_instance_capacity,
                nullptr,
                false,
                "geometry_buffer_instances"
            ) : nullptr;

            bool allocation_failed = (new_vertex_buffer && !new_vertex_buffer->GetRhiResource())              ||
                                     (new_index_buffer && !new_index_buffer->GetRhiResource())               ||
                                     (new_meshlet_bounds_buffer && !new_meshlet_bounds_buffer->GetRhiResource())      ||
                                     (new_meshlet_vertex_buffer && !new_meshlet_vertex_buffer->GetRhiResource())      ||
                                     (new_meshlet_micro_index_buffer && !new_meshlet_micro_index_buffer->GetRhiResource()) ||
                                     (new_instance_buffer && !new_instance_buffer->GetRhiResource());
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

                // drop the pending instance tail back to whatever the last successful gpu commit holds (zero on a first-build failure, identity slot 0 is reseeded on the next append)
                // without this, instances would stay at the failed size and the next rebuild would re-attempt the same too-big alloc
                instances.tail.clear();

                dirty       = false;
                was_rebuilt = false;
                return;
            }

            // Report input-buffer replacement. Completed BLAS remain valid because
            // arena growth preserves geometry contents and offsets.
            was_rebuilt = new_vertex_buffer != nullptr || new_index_buffer != nullptr;

            if (new_vertex_buffer)
            {
                adopt(vertex_buffer, new_vertex_buffer, static_cast<uint64_t>(vertices.committed) * sizeof(RHI_Vertex_PosTexNorTan));
                vertex_capacity = new_vertex_capacity;
            }
            if (new_index_buffer)
            {
                adopt(index_buffer, new_index_buffer, static_cast<uint64_t>(indices.committed) * sizeof(uint32_t));
                index_capacity = new_index_capacity;
            }
            if (new_meshlet_bounds_buffer)
            {
                adopt(meshlet_bounds_buffer, new_meshlet_bounds_buffer, static_cast<uint64_t>(meshlet_bounds.committed) * sizeof(Sb_MeshletBounds));
                meshlet_bounds_capacity = new_meshlet_bounds_capacity;
            }
            if (new_meshlet_vertex_buffer)
            {
                adopt(meshlet_vertex_buffer, new_meshlet_vertex_buffer, static_cast<uint64_t>(meshlet_vertices.committed) * sizeof(uint32_t));
                meshlet_vertex_capacity = new_meshlet_vertex_capacity;
            }
            if (new_meshlet_micro_index_buffer)
            {
                adopt(meshlet_micro_index_buffer, new_meshlet_micro_index_buffer, meshlet_micro_indices.committed);
                meshlet_micro_capacity = new_meshlet_micro_capacity;
            }
            if (new_instance_buffer)
            {
                adopt(instance_buffer, new_instance_buffer, static_cast<uint64_t>(instances.committed) * sizeof(Instance));
                instance_capacity = new_instance_capacity;
            }

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

        // Appends already store the exact packed micro bytes, including block padding.
        upload_tail(vertices, vertex_buffer.get());
        upload_tail(indices, index_buffer.get());
        upload_tail(meshlet_bounds, meshlet_bounds_buffer.get());
        upload_tail(meshlet_vertices, meshlet_vertex_buffer.get());
        upload_tail(meshlet_micro_indices, meshlet_micro_index_buffer.get());
        upload_tail(instances, instance_buffer.get());

        dirty = false;
    }

    bool GeometryBuffer::WasRebuilt()
    {
        bool result = was_rebuilt;
        was_rebuilt = false;
        return result;
    }

    uint64_t GeometryBuffer::GetCpuBytes()
    {
        lock_guard<mutex> lock(buffer_mutex);
        uint64_t bytes = vertices.tail.capacity() * sizeof(RHI_Vertex_PosTexNorTan) +
            indices.tail.capacity() * sizeof(uint32_t) +
            meshlet_bounds.tail.capacity() * sizeof(Sb_MeshletBounds) +
            meshlet_vertices.tail.capacity() * sizeof(uint32_t) +
            meshlet_micro_indices.tail.capacity() +
            instances.tail.capacity() * sizeof(Instance);
        for (const auto& [offset, data] : vertex_writes) bytes += data.capacity() * sizeof(RHI_Vertex_PosTexNorTan);
        for (const auto& [offset, data] : instance_writes) bytes += data.capacity() * sizeof(Instance);
        for (const auto& [offset, history] : vertex_history) bytes += history.pose.capacity() * sizeof(RHI_Vertex_PosTexNorTan);
        return bytes;
    }

    uint64_t GeometryBuffer::GetPendingUploadBytes()
    {
        lock_guard<mutex> lock(buffer_mutex);
        return vertices.tail.size() * sizeof(RHI_Vertex_PosTexNorTan) +
            indices.tail.size() * sizeof(uint32_t) +
            meshlet_bounds.tail.size() * sizeof(Sb_MeshletBounds) +
            meshlet_vertices.tail.size() * sizeof(uint32_t) +
            meshlet_micro_indices.tail.size() +
            instances.tail.size() * sizeof(Instance);
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

    void GeometryBuffer::ReserveForWorldLoad(const string& resources)
    {
        vector<uint32_t> counts;
        constexpr uint64_t key = 1;
        if (!generated_cache::Load(generated_cache::Path(resources, "geometry_capacity", key), key, counts) || counts.size() != 6) return;
        const uint64_t bytes = uint64_t(counts[0]) * sizeof(RHI_Vertex_PosTexNorTan) +
            uint64_t(counts[1]) * sizeof(uint32_t) + uint64_t(counts[2]) * sizeof(Sb_MeshletBounds) +
            uint64_t(counts[3]) * sizeof(uint32_t) + uint64_t(counts[4]) * sizeof(uint8_t) + uint64_t(counts[5]) * sizeof(Instance);
        // A corrupt/stale hint must not request an unbounded allocation. the floors
        // size the first gpu allocation only; appends still initialize and validate all data.
        if (bytes > 16ull * 1024 * 1024 * 1024) return;
        Reserve(counts[0], counts[1], counts[2], counts[3], counts[4], counts[5]);
    }

    void GeometryBuffer::SaveWorldLoadCapacity(const string& resources)
    {
        vector<uint32_t> counts;
        {
            lock_guard<mutex> lock(buffer_mutex);
            counts = {vertices.size(), indices.size(), meshlet_bounds.size(), meshlet_vertices.size(), meshlet_micro_indices.size(), instances.size()};
        }
        constexpr uint64_t key = 1;
        generated_cache::Save(generated_cache::Path(resources, "geometry_capacity", key), key, counts);
    }

    void GeometryBuffer::Shutdown()
    {
        vertex_buffer              = nullptr;
        index_buffer               = nullptr;
        meshlet_bounds_buffer      = nullptr;
        meshlet_vertex_buffer      = nullptr;
        meshlet_micro_index_buffer = nullptr;
        instance_buffer            = nullptr;
        vertex_history.clear();
        vertices.reset();
        indices.reset();
        meshlet_bounds.reset();
        meshlet_vertices.reset();
        meshlet_micro_indices.reset();
        instances.reset();
        vertex_writes.clear();
        instance_writes.clear();
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
