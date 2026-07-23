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

#pragma once

//= INCLUDES =====================
#include <vector>
#include <mutex>
#include <memory>
#include "../RHI/RHI_Vertex.h"
#include "Renderer_Buffers.h"
#include "Instance.h"
//================================

namespace spartan
{
    class RHI_Buffer;

    // single global vertex and index buffer for all mesh geometry in the world.
    // meshes append their data here during loading and receive base offsets.
    // gpu buffers are pre-allocated with headroom so that late-arriving meshes
    // can be uploaded via sub-region copies without recreating the entire buffer.
    class GeometryBuffer
    {
    public:
        // append vertices to the global buffer, returns the base vertex offset
        static uint32_t AppendVertices(const RHI_Vertex_PosTexNorTan* data, uint32_t count);

        // append indices to the global buffer, returns the base index offset
        static uint32_t AppendIndices(const uint32_t* data, uint32_t count);

        // append meshlet bounds to the global buffer, returns the base meshlet offset
        static uint32_t AppendMeshletBounds(const Sb_MeshletBounds* data, uint32_t count);

        // append instances to the global instance buffer, returns the base instance offset
        // index 0 is reserved for the identity instance used by non-instanced draws
        static uint32_t AppendInstances(const Instance* data, uint32_t count);

        // update existing vertices in-place (cpu + gpu), used by deformable meshes like cloth
        static void UpdateVertices(const RHI_Vertex_PosTexNorTan* data, uint32_t offset, uint32_t count);
        static void UpdateIndices(
            const uint32_t* data,
            uint32_t offset,
            uint32_t count
        );
        static void UpdateMeshletBounds(
            const Sb_MeshletBounds* data,
            uint32_t offset,
            uint32_t count
        );

        // synchronize gpu buffers with cpu data:
        //  - if no gpu buffer exists, create one with headroom and upload everything
        //  - if new data fits within existing capacity, upload only the new portion
        //  - if capacity is exceeded, recreate with headroom
        static void BuildIfDirty();

        // bump capacity floors so the next BuildIfDirty allocates large enough buffers up-front
        // can be called before world load with a budget so we avoid mid-load rebuilds
        static void Reserve(uint32_t vertex_count, uint32_t index_count, uint32_t meshlet_bounds_count, uint32_t instance_count);

        // destroy gpu buffers and clear cpu data
        static void Shutdown();

        static RHI_Buffer* GetVertexBuffer();
        static RHI_Buffer* GetIndexBuffer();
        static RHI_Buffer* GetMeshletBoundsBuffer();
        static RHI_Buffer* GetInstanceBuffer();

        // returns true if a full buffer rebuild occurred this frame (capacity exceeded).
        // callers should use this to invalidate caches that depend on buffer addresses (e.g. acceleration structures).
        // the flag is cleared after being read.
        static bool WasRebuilt();

    private:
        // cpu-side accumulators
        static std::vector<RHI_Vertex_PosTexNorTan> m_vertices;
        static std::vector<uint32_t> m_indices;
        static std::vector<Sb_MeshletBounds> m_meshlet_bounds;
        static std::vector<Instance> m_instances;

        // gpu buffers
        static std::unique_ptr<RHI_Buffer> m_vertex_buffer;
        static std::unique_ptr<RHI_Buffer> m_index_buffer;
        static std::unique_ptr<RHI_Buffer> m_meshlet_bounds_buffer;
        static std::unique_ptr<RHI_Buffer> m_instance_buffer;

        // capacity tracking (element counts)
        static uint32_t m_vertex_count_committed; // elements uploaded to gpu
        static uint32_t m_index_count_committed;
        static uint32_t m_meshlet_bounds_count_committed;
        static uint32_t m_instance_count_committed;
        static uint32_t m_vertex_capacity;        // total gpu buffer capacity
        static uint32_t m_index_capacity;
        static uint32_t m_meshlet_bounds_capacity;
        static uint32_t m_instance_capacity;

        // state
        static bool m_dirty;
        static bool m_was_rebuilt;
        static std::mutex m_mutex;

        // hard-cap learned from previous oom failures, prevents retrying ever-larger allocations every frame
        // once set, AppendInstances drops new instances and BuildIfDirty stops attempting to grow past it
        static uint32_t m_instance_capacity_failed_at;

        // logs the geometry buffer oom error exactly once per session so async grass tile arrivals don't spam the same message
        static bool m_oom_logged;

        // growth factor applied when allocating gpu buffers
        static constexpr float growth_factor = 1.25f;
    };
}
