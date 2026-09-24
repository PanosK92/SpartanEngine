/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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

    struct GpuMemoryDetail
    {
        uint32_t width     = 0;
        uint32_t height    = 0;
        uint32_t depth     = 0;
        uint32_t mip_count = 0;
        const char* format = nullptr;
        const char* path   = nullptr;
        const char* type   = nullptr;
    };

    struct GpuMemoryBlock
    {
        void* resource     = nullptr;
        uint64_t size      = 0;
        uint64_t offset    = 0;
        uint64_t heap_id   = 0;
        uint64_t heap_size = 0;
        GpuMemoryKind kind = GpuMemoryKind::Other;
        uint32_t width     = 0;
        uint32_t height    = 0;
        uint32_t depth     = 0;
        uint32_t mip_count = 0;
        char name[96]      = {};
        char format[32]    = {};
        char path[160]     = {};
        char type[16]      = {};
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
            uint64_t heap_size = 0,
            const GpuMemoryDetail& detail = {}
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
