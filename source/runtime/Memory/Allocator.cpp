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


//= INCLUDES ====================
#include "pch.h"
#include "Allocator.h"
#if defined(_WIN32)
#include <Windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#elif defined(__linux__)
#include <unistd.h>
#include <sys/resource.h>
#endif
//===============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        atomic<size_t> bytes_allocated      = 0;
        atomic<size_t> bytes_allocated_peak = 0;
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
        bytes_allocated.fetch_add(size, memory_order_relaxed);
        bytes_allocated_peak = max(bytes_allocated_peak.load(memory_order_relaxed), bytes_allocated.load(memory_order_relaxed));

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
        bytes_allocated.fetch_sub(size, memory_order_relaxed);

#if defined(_MSC_VER)
        _aligned_free(raw);
#else
        free(raw);
#endif
    }

    void Allocator::Tick()
    {
        static bool has_warned                    = false; // only warn once per threshold crossing
        constexpr float warning_threshold_percent = 90.0f; // 90%
    
        float total_mb     = GetMemoryAvailableMb();
        float used_mb      = GetMemoryAllocatedMb();
        float used_percent = (used_mb / total_mb) * 100.0f;
    
        if (!has_warned && used_percent >= warning_threshold_percent)
        {
            float available_mb = GetMemoryAvailableMb();
            SP_LOG_WARNING(
                "Warning: High memory usage %.1f%% (%.1f MB used of %.1f MB). "
                "Available memory: %.1f MB. "
                "New allocations may be slower due to paging.\n",
                used_percent, used_mb, total_mb, available_mb
            );
            has_warned = true;
        }
    
        // reset warning if usage drops below threshold
        if (has_warned && used_percent < warning_threshold_percent - 5.0f)
        {
            has_warned = false;
        }
    }

    float Allocator::GetMemoryAllocatedMb()
    {
         return static_cast<float>(bytes_allocated) / (1024.0f * 1024.0f);
    }

    float Allocator::GetMemoryProcessUsedMb()
    {
    #if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
        {
            // working set = physical memory currently used by the process
            return static_cast<float>(pmc.WorkingSetSize) / (1024.0f * 1024.0f);
        }
        return 0.0f;
    
    #elif defined(__linux__)
        // Read from /proc/self/statm
        long rss_pages = 0;
        FILE* file = fopen("/proc/self/statm", "r");
        if (file)
        {
            long total_pages = 0;
            if (fscanf(file, "%ld %ld", &total_pages, &rss_pages) == 2)
            {
                fclose(file);
                long page_size = sysconf(_SC_PAGE_SIZE);
                return static_cast<float>(rss_pages * page_size) / (1024.0f * 1024.0f);
            }
            fclose(file);
        }
        return 0.0f;
    
    #else
        return 0.0f; // unsupported platform
    #endif
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

    float Allocator::GetMemoryAllocatedPeakMb()
    {
        return static_cast<float>(bytes_allocated_peak) / (1024.0f * 1024.0f);
    }
}
