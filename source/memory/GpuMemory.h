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

//= INCLUDES =====
#include <cstdint>
#include <vector>
//================

namespace spartan
{
    enum class GpuMemoryKind : uint8_t
    {
        Texture = 0,
        Vertex,
        Index,
        Instance,
        Storage,
        Constant,
        Upload,
        Readback,
        ShaderBindingTable,
        AccelerationStructure,
        Other,
        Count
    };

    struct GpuMemoryBlock
    {
        void* resource     = nullptr;
        uint64_t size      = 0;
        uint64_t offset    = 0;
        uint64_t heap_id   = 0;
        uint64_t heap_size = 0;
        GpuMemoryKind kind = GpuMemoryKind::Other;
        char name[96]      = {};
    };

    class GpuMemory
    {
    public:
        static void Register(
            void* resource,
            uint64_t size,
            GpuMemoryKind kind,
            const char* name,
            uint64_t offset = 0,
            uint64_t heap_id = 0,
            uint64_t heap_size = 0
        );

        static void Unregister(void* resource);
        static void Clear();

        static void GetBlocks(std::vector<GpuMemoryBlock>& out);
        static uint64_t GetAllocatedBytes();
        static uint32_t GetAllocationCount();

        static GpuMemoryKind FromBufferType(uint32_t buffer_type);
        static const char* GetKindName(GpuMemoryKind kind);
    };
}
