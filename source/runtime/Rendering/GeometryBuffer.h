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
#include "../RHI/RHI_Vertex.h"
#include "Renderer_Buffers.h"
#include "Instance.h"
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

        // append instances to the global instance buffer, returns the base instance offset
        // index 0 is reserved for the identity instance used by non-instanced draws
        static uint32_t AppendInstances(const Instance* data, uint32_t count);

        // update existing vertices in-place, used by deformable meshes like cloth and skinning
        // the cpu copy is immediate, the gpu copy is queued and coalesced by the next BuildIfDirty
        static void UpdateVertices(const RHI_Vertex_PosTexNorTan* data, uint32_t offset, uint32_t count);
        static void UpdateIndices(const uint32_t* data, uint32_t offset, uint32_t count);
        static void UpdateMeshletBounds(const Sb_MeshletBounds* data, uint32_t offset, uint32_t count);

        // uploads only the new portion when it fits the existing capacity, otherwise recreates with headroom
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

        // true when capacity was exceeded and the buffers moved, invalidates address dependent caches, cleared on read
        static bool WasRebuilt();
    };
}
