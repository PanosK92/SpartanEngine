/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====
#include <cstddef>
#include <cstdint>
//================

namespace spartan
{
    // memory tags for tracking allocations by subsystem
    enum class MemoryTag : uint8_t
    {
        Untagged = 0,
        Rendering,
        Physics,
        Audio,
        Scripting,
        Resources,
        World,
        Ui,
        Count
    };

    class Allocator
    {
    public:
        // allocate aligned memory with optional tag for tracking
        static void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t), MemoryTag tag = MemoryTag::Untagged);

        // free previously allocated memory
        static void Free(void* ptr);

        // called once per frame
        static void Tick();

        // total memory allocated by the engine
        static float GetMemoryAllocatedMb();

        // peak memory allocated by the engine
        static float GetMemoryAllocatedPeakMb();

        // total memory used by the process including engine, dlls, drivers, os allocations, etc.
        static float GetMemoryProcessUsedMb();

        // available physical system memory
        static float GetMemoryAvailableMb();

        // total physical system memory
        static float GetMemoryTotalMb();

        // memory allocated by a specific tag/subsystem
        static float GetMemoryAllocatedByTagMb(MemoryTag tag);

        // get tag name as string
        static const char* GetTagName(MemoryTag tag);
    };
}
