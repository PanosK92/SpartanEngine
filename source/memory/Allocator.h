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
#include <string>
#include <vector>
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

    struct AllocationSite
    {
        std::string call_stack;
        uint64_t allocations_total = 0; // since the census was last started
        int64_t live_allocations   = 0;
        int64_t live_bytes         = 0;
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

        // returns free heap pages to the os, call it after large frees such as a world unload
        // the calling thread collects now, every other thread on its next allocation
        static void Collect();

        // total memory allocated by the engine
        static float GetMemoryAllocatedMb();

        // peak memory allocated by the engine, sampled at least once per frame
        static float GetMemoryAllocatedPeakMb();

        // number of live allocations
        static uint64_t GetAllocationCount();

        // number of allocations made between the last two calls to Tick()
        static uint64_t GetAllocationsLastFrame();

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

        // logs the call sites holding the most live bytes, only records when SPARTAN_HEAP_CENSUS=1 is set
        static void LogLargestAllocationSites(const char* label, uint32_t count);

        // records the call stack of every allocation of min_size bytes or more, slow, for diagnostics only (windows)
        static void SetAllocationCensus(bool enabled, std::size_t min_size);
        static bool IsAllocationCensusEnabled();

        // call sites that made the most allocations since the census was started
        static std::vector<AllocationSite> GetBusiestAllocationSites(uint32_t count);
    };
}
