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


//= INCLUDES ====================
#include "pch.h"
#include "Allocator.h"
#include <cstring>
#include <cstdlib>
#if defined(_WIN32)
#include <Windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#elif defined(__linux__)
#include <unistd.h>
#include <sys/resource.h>
#include <malloc.h>
#endif
//===============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        // magic number to detect double-free and corruption
        constexpr uint32_t allocation_magic_active = 0xABCD1234;
        constexpr uint32_t allocation_magic_freed  = 0xDEADBEEF;

        // poison patterns for debug builds
        constexpr unsigned char poison_allocated = 0xCD; // freshly allocated memory
        constexpr unsigned char poison_freed     = 0xDD; // freed memory

        // thread-local cache settings
        constexpr size_t cache_max_size       = 256;  // max allocation size to cache
        constexpr size_t cache_max_entries    = 32;   // max entries per size class
        constexpr size_t cache_size_classes   = 8;    // number of size classes: 32, 64, 96, 128, 160, 192, 224, 256
        constexpr size_t cache_size_granularity = 32; // size class granularity

        // global counters
        atomic<size_t> bytes_allocated      = 0;
        atomic<size_t> bytes_allocated_peak = 0;
        atomic<size_t> allocation_count     = 0;

        // per-tag counters
        atomic<size_t> bytes_by_tag[static_cast<size_t>(MemoryTag::Count)] = {};

        // header stores allocation metadata
        struct allocation_header
        {
            uint32_t  magic;  // magic number for corruption/double-free detection
            uint32_t  offset; // bytes from raw allocation to user pointer (32-bit is enough)
            size_t    size;   // requested size
            MemoryTag tag;    // memory tag for tracking
            uint8_t   padding[7]; // pad to maintain alignment
        };

        // thread-local cache entry
        struct cache_entry
        {
            void*  ptr;
            size_t size;
        };

        // thread-local free list for small allocations
        struct thread_cache
        {
            cache_entry entries[cache_size_classes][cache_max_entries];
            size_t      count[cache_size_classes] = {};
        };

        thread_local thread_cache tl_cache = {};

        // get size class index (0-7 for sizes 1-256)
        size_t get_size_class(size_t size)
        {
            if (size == 0)
                return 0;
            return min((size - 1) / cache_size_granularity, cache_size_classes - 1);
        }

        // get actual size for a size class
        size_t get_size_for_class(size_t size_class)
        {
            return (size_class + 1) * cache_size_granularity;
        }

        // try to get from thread-local cache
        void* cache_try_get(size_t size)
        {
            if (size > cache_max_size)
                return nullptr;

            size_t size_class = get_size_class(size);
            if (tl_cache.count[size_class] > 0)
            {
                tl_cache.count[size_class]--;
                return tl_cache.entries[size_class][tl_cache.count[size_class]].ptr;
            }
            return nullptr;
        }

        // try to put into thread-local cache, returns true if cached
        bool cache_try_put(void* ptr, size_t size)
        {
            if (size > cache_max_size)
                return false;

            size_t size_class = get_size_class(size);
            if (tl_cache.count[size_class] < cache_max_entries)
            {
                tl_cache.entries[size_class][tl_cache.count[size_class]].ptr  = ptr;
                tl_cache.entries[size_class][tl_cache.count[size_class]].size = size;
                tl_cache.count[size_class]++;
                return true;
            }
            return false;
        }

        // atomically update peak if current value is higher
        void update_peak(size_t current)
        {
            size_t peak = bytes_allocated_peak.load(memory_order_relaxed);
            while (current > peak && !bytes_allocated_peak.compare_exchange_weak(peak, current, memory_order_relaxed, memory_order_relaxed))
            {
                // peak is updated by compare_exchange_weak on failure
            }
        }

        // round up to next multiple of alignment
        size_t align_up(size_t value, size_t alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        // perform the actual allocation (bypassing cache)
        void* allocate_internal(size_t size, size_t alignment, MemoryTag tag)
        {
#if defined(__linux__)
            // ABI-safe End-Header implementation for Linux
            const size_t header_size = sizeof(allocation_header);
            const size_t total_size  = size + header_size;

            void* ptr = nullptr;
            if (alignment <= alignof(std::max_align_t))
            {
                ptr = malloc(total_size);
            }
            else
            {
                // ensure alignment is a power of 2
                if ((alignment & (alignment - 1)) != 0)
                {
                    size_t p2 = 1;
                    while (p2 < alignment) p2 <<= 1;
                    alignment = p2;
                }
                if (posix_memalign(&ptr, alignment, align_up(total_size, alignment)) != 0)
                    return nullptr;
            }

            if (!ptr)
                return nullptr;

            // store header at the very end of the usable space
            size_t usable_size = malloc_usable_size(ptr);
            allocation_header* header = reinterpret_cast<allocation_header*>(static_cast<char*>(ptr) + usable_size - header_size);
            header->magic  = allocation_magic_active;
            header->offset = 0; // indicates End-Header format
            header->size   = size;
            header->tag    = tag;

#if defined(_DEBUG) || defined(DEBUG)
            memset(ptr, poison_allocated, size);
#endif

            // update counters
            size_t current = bytes_allocated.fetch_add(usable_size, memory_order_relaxed) + usable_size;
            update_peak(current);
            allocation_count.fetch_add(1, memory_order_relaxed);
            bytes_by_tag[static_cast<size_t>(tag)].fetch_add(usable_size, memory_order_relaxed);

            return ptr;
#else
            // ensure minimum alignment for our header
            alignment = max(alignment, alignof(allocation_header));

            // we need space for: header + padding + user data
            const size_t header_size = sizeof(allocation_header);
            const size_t total_size  = size + header_size + alignment; // worst case padding

#if defined(_MSC_VER)
            void* raw = _aligned_malloc(total_size, alignment);
#else
            // aligned_alloc requires size to be a multiple of alignment
            const size_t aligned_total_size = align_up(total_size, alignment);
            void* raw = aligned_alloc(alignment, aligned_total_size);
#endif
            if (!raw)
                return nullptr;

            // calculate aligned user pointer (must be aligned and have room for header before it)
            uintptr_t raw_addr  = reinterpret_cast<uintptr_t>(raw);
            uintptr_t user_addr = align_up(raw_addr + header_size, alignment);
            void* user_ptr      = reinterpret_cast<void*>(user_addr);

            // store header just before user pointer
            allocation_header* header = reinterpret_cast<allocation_header*>(user_addr - header_size);
            header->magic  = allocation_magic_active;
            header->offset = static_cast<uint32_t>(user_addr - raw_addr);
            header->size   = size;
            header->tag    = tag;

#if defined(_DEBUG) || defined(DEBUG)
            // poison allocated memory in debug builds to catch uninitialized reads
            memset(user_ptr, poison_allocated, size);
#endif

            // update counters
            size_t current = bytes_allocated.fetch_add(size, memory_order_relaxed) + size;
            update_peak(current);
            allocation_count.fetch_add(1, memory_order_relaxed);
            bytes_by_tag[static_cast<size_t>(tag)].fetch_add(size, memory_order_relaxed);

            return user_ptr;
#endif
        }

        // perform the actual free (bypassing cache)
        void free_internal(void* ptr)
        {
#if defined(__linux__)
            // End-Header implementation for Linux
            // This makes it ABI safe, some system libraries were crashing without this.
            // On Linux we may receive foreign pointers from driver code (e.g., Mesa/OpenGL),
            // so we fallback to free() instead of asserting to avoid crashes from untrusted pointers.
            size_t usable_size = malloc_usable_size(ptr);
            const size_t header_size = sizeof(allocation_header);
            
            allocation_header* header = reinterpret_cast<allocation_header*>(static_cast<char*>(ptr) + usable_size - header_size);

            if (header->magic != allocation_magic_active || header->offset != 0)
            {
                SP_LOG_WARNING("Foreign pointer detected in allocator, falling back to free()");
                ::free(ptr);
                return;
            }

            header->magic = allocation_magic_freed;
            bytes_allocated.fetch_sub(usable_size, memory_order_relaxed);
            allocation_count.fetch_sub(1, memory_order_relaxed);
            bytes_by_tag[static_cast<size_t>(header->tag)].fetch_sub(usable_size, memory_order_relaxed);

            ::free(ptr);
#else
            const size_t header_size = sizeof(allocation_header);

            // read header just before user pointer
            allocation_header* header = reinterpret_cast<allocation_header*>(static_cast<char*>(ptr) - header_size);

            // check for double-free
            if (header->magic == allocation_magic_freed)
            {
                SP_LOG_ERROR("Double-free detected at address %p", ptr);
                SP_ASSERT(false && "double-free detected");
                return;
            }

            // check for corruption
            if (header->magic != allocation_magic_active)
            {
                SP_LOG_ERROR("Memory corruption detected at address %p (magic: 0x%08X)", ptr, header->magic);
                SP_ASSERT(false && "memory corruption detected");
                return;
            }

            size_t    size   = header->size;
            uint32_t  offset = header->offset;
            MemoryTag tag    = header->tag;

            // mark as freed before actually freeing
            header->magic = allocation_magic_freed;

#if defined(_DEBUG) || defined(DEBUG)
            // poison freed memory in debug builds to catch use-after-free
            memset(ptr, poison_freed, size);
#endif

            // calculate original raw pointer
            void* raw = static_cast<char*>(ptr) - offset;

            // update counters
            bytes_allocated.fetch_sub(size, memory_order_relaxed);
            allocation_count.fetch_sub(1, memory_order_relaxed);
            bytes_by_tag[static_cast<size_t>(tag)].fetch_sub(size, memory_order_relaxed);

#if defined(_MSC_VER)
            _aligned_free(raw);
#else
            ::free(raw);
#endif
#endif
        }
    }

    void* Allocator::Allocate(size_t size, size_t alignment, MemoryTag tag)
    {
        // try thread-local cache first for small allocations with default alignment
        if (alignment <= alignof(allocation_header) && size <= cache_max_size)
        {
            size_t size_class   = get_size_class(size);
            size_t padded_size  = get_size_for_class(size_class);
            void*  cached       = cache_try_get(padded_size);
            if (cached)
            {
                // update header with new tag (size stays the same since it's from same size class)
                const size_t header_size  = sizeof(allocation_header);
#if defined(__linux__)
                size_t usable_size = malloc_usable_size(cached);
                allocation_header* header = reinterpret_cast<allocation_header*>(static_cast<char*>(cached) + usable_size - header_size);
#else
                allocation_header* header = reinterpret_cast<allocation_header*>(static_cast<char*>(cached) - header_size);
#endif
                
                // re-activate the allocation
                header->magic = allocation_magic_active;
                header->tag   = tag;

                // update tag counter (size is already counted from original allocation)
#if defined(__linux__)
                bytes_by_tag[static_cast<size_t>(tag)].fetch_add(usable_size, memory_order_relaxed);
#else
                bytes_by_tag[static_cast<size_t>(tag)].fetch_add(header->size, memory_order_relaxed);
#endif

#if defined(_DEBUG) || defined(DEBUG)
                memset(cached, poison_allocated, header->size);
#endif
                return cached;
            }
            // cache miss - allocate with padded size for future caching
            return allocate_internal(padded_size, alignment, tag);
        }

        return allocate_internal(size, alignment, tag);
    }

    void Allocator::Free(void* ptr)
    {
        if (!ptr)
            return;

        const size_t header_size = sizeof(allocation_header);
#if defined(__linux__)
        size_t usable_size = malloc_usable_size(ptr);
        allocation_header* header = reinterpret_cast<allocation_header*>(static_cast<char*>(ptr) + usable_size - header_size);
#else
        allocation_header* header = reinterpret_cast<allocation_header*>(static_cast<char*>(ptr) - header_size);
#endif

        // validate before accessing other fields
        if (header->magic == allocation_magic_freed)
        {
            SP_LOG_ERROR("Double-free detected at address %p", ptr);
            SP_ASSERT(false && "double-free detected");
            return;
        }

#if defined(__linux__)
        if (header->magic != allocation_magic_active || header->offset != 0)
        {
            // Foreign pointer (not allocated by our allocator) - log and free
            SP_LOG_WARNING("Foreign pointer free at %p (magic: 0x%08X, offset: %zu)", ptr, header->magic, header->offset);
            ::free(ptr);
            return;
        }
#else
        if (header->magic != allocation_magic_active)
        {
            SP_LOG_ERROR("Memory corruption detected at address %p (magic: 0x%08X)", ptr, header->magic);
            SP_ASSERT(false && "memory corruption detected");
            return;
        }
#endif

        size_t size = header->size;

        // update tag counter before potential caching
#if defined(__linux__)
        bytes_by_tag[static_cast<size_t>(header->tag)].fetch_sub(usable_size, memory_order_relaxed);
#else
        bytes_by_tag[static_cast<size_t>(header->tag)].fetch_sub(size, memory_order_relaxed);
#endif

        // try to cache small allocations
        if (size <= cache_max_size)
        {
            // mark as freed but keep in cache
            header->magic = allocation_magic_freed;

#if defined(_DEBUG) || defined(DEBUG)
            memset(ptr, poison_freed, size);
#endif

            if (cache_try_put(ptr, size))
            {
                // successfully cached - don't actually free
                // note: counters stay as-is since memory is still "allocated" from system perspective
                return;
            }
            // cache full - restore magic and fall through to actual free
            header->magic = allocation_magic_active;
        }

        free_internal(ptr);
    }

    void Allocator::Tick()
    {
        static bool has_warned                    = false; // only warn once per threshold crossing
        constexpr float warning_threshold_percent = 90.0f; // 90%
    
        float total_mb     = GetMemoryTotalMb();
        float used_mb      = GetMemoryProcessUsedMb();
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
        long page_size  = sysconf(_SC_PAGE_SIZE);
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
        long pages     = sysconf(_SC_PHYS_PAGES);
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

    float Allocator::GetMemoryAllocatedByTagMb(MemoryTag tag)
    {
        size_t index = static_cast<size_t>(tag);
        if (index >= static_cast<size_t>(MemoryTag::Count))
            return 0.0f;
        return static_cast<float>(bytes_by_tag[index].load(memory_order_relaxed)) / (1024.0f * 1024.0f);
    }

    const char* Allocator::GetTagName(MemoryTag tag)
    {
        static const char* tag_names[] =
        {
            "Untagged",
            "Rendering",
            "Physics",
            "Audio",
            "Scripting",
            "Resources",
            "World",
            "Ui"
        };

        size_t index = static_cast<size_t>(tag);
        if (index >= static_cast<size_t>(MemoryTag::Count))
            return "Unknown";
        return tag_names[index];
    }
}
