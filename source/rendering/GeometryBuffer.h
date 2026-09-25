/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include "../rhi/RHI_Vertex.h"
#include "Renderer_Buffers.h"
#include "Instance.h"
#include <string>
//================================

namespace spartan
{
    class RHI_Buffer;

    // one global vertex and index buffer for all world geometry, meshes append during loading and receive base offsets
    class GeometryBuffer
    {
    public:
        // append vertices to the global buffer, returns the base vertex offset
        static uint32_t AppendVertices(const RHI_Vertex_PosTexNorTan* data, uint32_t count);

        // append indices to the global buffer, returns the base index offset
        static uint32_t AppendIndices(const uint32_t* data, uint32_t count);

        // append meshlet bounds to the global buffer, returns the base meshlet offset
        static uint32_t AppendMeshletBounds(const Sb_MeshletBounds* data, uint32_t count);

        // append meshlet unique-vertex remaps, returns the base offset
        static uint32_t AppendMeshletVertices(const uint32_t* data, uint32_t count);

        // append meshlet micro-indices, returns the base offset
        static uint32_t AppendMeshletMicroIndices(const uint32_t* data, uint32_t count);

        // append instances to the global instance buffer, returns the base instance offset
        // index 0 is reserved for the identity instance used by non-instanced draws
        static uint32_t AppendInstances(const Instance* data, uint32_t count);

        // overwrite instances in-place inside a range a previous append returned, false when the range is invalid
        // the cpu copy is immediate, the gpu copy is queued and coalesced by the next BuildIfDirty
        static bool UpdateInstances(const Instance* data, uint32_t offset, uint32_t count);

        // update existing vertices in-place, used by deformable meshes like cloth and skinning
        // the cpu copy is immediate, the gpu copy is queued and coalesced by the next BuildIfDirty
        // track_motion retains the previous rendered pose for a stable, whole-mesh range
        static void UpdateVertices(const RHI_Vertex_PosTexNorTan* data, uint32_t offset, uint32_t count, bool track_motion = false);

        // relative offset into the same vertex arena, zero when this frame has no deformation
        static uint32_t GetPreviousVertexOffset(uint32_t offset);
        static void UpdateIndices(const uint32_t* data, uint32_t offset, uint32_t count);
        static void UpdateMeshletBounds(const Sb_MeshletBounds* data, uint32_t offset, uint32_t count);
        static void UpdateMeshletVertices(const uint32_t* data, uint32_t offset, uint32_t count);
        static void UpdateMeshletMicroIndices(const uint32_t* data, uint32_t offset, uint32_t count);

        // uploads only the new portion when it fits the existing capacity, otherwise recreates with headroom
        static void BuildIfDirty();

        // request capacity floors, the next BuildIfDirty grows the gpu buffers if they are smaller
        // can be called before world load with a budget so we avoid mid-load rebuilds
        static void Reserve(
            uint32_t vertex_count,
            uint32_t index_count,
            uint32_t meshlet_bounds_count,
            uint32_t meshlet_vertex_count,
            uint32_t meshlet_micro_count,
            uint32_t instance_count
        );

        // Capacity hints only: stale/missing hints never affect geometry or offsets.
        static void ReserveForWorldLoad(const std::string& resources);
        static void SaveWorldLoadCapacity(const std::string& resources);

        // destroy gpu buffers and clear cpu data
        static void Shutdown();

        static RHI_Buffer* GetVertexBuffer();
        static RHI_Buffer* GetIndexBuffer();
        static RHI_Buffer* GetMeshletBoundsBuffer();
        static RHI_Buffer* GetMeshletVertexBuffer();
        static RHI_Buffer* GetMeshletMicroIndexBuffer();
        static RHI_Buffer* GetInstanceBuffer();

        // true when capacity was exceeded and the buffers moved, invalidates address dependent caches, cleared on read
        static bool WasRebuilt();

        // heap held by the cpu side accumulators
        static uint64_t GetCpuBytes();

        // appended bytes still waiting for BuildIfDirty
        static uint64_t GetPendingUploadBytes();
    };
}
