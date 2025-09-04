/*
Copyright(c) 2015-2025 Panos Karabelas

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


//= INCLUDES ============
#include "pch.h"
#include "Allocator.h"
#if defined(_WIN32)
#include <Windows.h>
#include <Psapi.h>
#elif defined(__linux__)
#include <unistd.h>
#include <sys/resource.h>
#endif
//=======================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        atomic<size_t> g_total_allocated = 0;
    }

    void* Allocator::Allocate(size_t size, size_t alignment)
    {
        // allocate extra space for storing size just before the aligned pointer
        const size_t header_size = sizeof(size_t);
        const size_t total_size  = size + header_size;

#if defined(_MSC_VER)
        void* raw = _aligned_malloc(total_size, alignment);
#else
        void* raw = aligned_alloc(alignment, total_size);
#endif
        if (!raw)
            return nullptr;

        // store size at the beginning
        *reinterpret_cast<size_t*>(raw) = size;

        // update counter
        g_total_allocated.fetch_add(size, memory_order_relaxed);

        // return pointer just after the header
        return static_cast<char*>(raw) + header_size;
    }

    void Allocator::Free(void* ptr)
    {
        if (!ptr)
            return;

        const size_t header_size = sizeof(size_t);

        // move back to get the header
        void* raw = static_cast<char*>(ptr) - header_size;

        // read stored size
        size_t size = *reinterpret_cast<size_t*>(raw);

        // update counter
        g_total_allocated.fetch_sub(size, memory_order_relaxed);

#if defined(_MSC_VER)
        _aligned_free(raw);
#else
        free(raw);
#endif
    }

    float Allocator::GetMemoryAllocatedMb()
    {
         return static_cast<float>(g_total_allocated) / (1024.0f * 1024.0f);
    }

    float Allocator::GetMemoryAvailableMb()
    {
    #if defined(_WIN32)
        MEMORYSTATUSEX status;
        status.dwLength = sizeof(status);
        GlobalMemoryStatusEx(&status);
        return static_cast<float>(status.ullAvailPhys) / (1024.0f * 1024.0f);
    #elif defined(__linux__)
        long free_pages = sysconf(_SC_AVPHYS_PAGES); // available pages
        long page_size = sysconf(_SC_PAGE_SIZE);
        return static_cast<float>(free_pages * page_size) / (1024.0f * 1024.0f);
    #else
        return 0.0f; // unsupported platform
    #endif
    }

    float Allocator::GetMemoryTotalMb()
    {
#if defined(_WIN32)
        MEMORYSTATUSEX status;
        status.dwLength = sizeof(status);
        GlobalMemoryStatusEx(&status);
        return static_cast<float>(status.ullTotalPhys) / (1024.0f * 1024.0f);
#elif defined(__linux__)
        long pages = sysconf(_SC_PHYS_PAGES);
        long page_size = sysconf(_SC_PAGE_SIZE);
        return static_cast<float>(pages * page_size) / (1024.0f * 1024.0f);
#else
        return 0.0f; // unsupported platform
#endif
    }
}
