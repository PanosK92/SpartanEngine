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
