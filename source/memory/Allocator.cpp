/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/


//= INCLUDES ====================
#include "pch.h"
#include "Allocator.h"
#include <cstring>
#if defined(_WIN32)
#include <Windows.h>
#include <psapi.h>
#include <DbgHelp.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dbghelp.lib")
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
            uint32_t  magic;      // magic number for corruption/double-free detection
            uint32_t  offset;     // bytes from raw allocation to user pointer (32-bit is enough)
            size_t    size;       // requested size
            MemoryTag tag;        // memory tag for tracking
            uint8_t   padding[5]; // pad to maintain alignment
            uint16_t  site;       // census slot + 1, 0 when untracked
        };

#if defined(_WIN32)
        // call site census, it can't allocate since it runs inside the allocator
        namespace census
        {
            constexpr size_t   min_size    = 16 * 1024;
            constexpr uint32_t frame_count = 20;
            constexpr uint32_t slot_count  = 8192;

            struct site
            {
                ULONG  hash;
                void*  frames[frame_count];
                USHORT frame_used;
                int64_t bytes;
                int64_t allocations;
            };

            site slots[slot_count] = {};
            uint32_t slots_used    = 0;
            SRWLOCK lock           = SRWLOCK_INIT;
            int enabled            = -1;

            bool is_enabled()
            {
                if (enabled < 0)
                {
                    char value[4] = {};
                    enabled = (GetEnvironmentVariableA("SPARTAN_HEAP_CENSUS", value, sizeof(value)) > 0 && value[0] == '1') ? 1 : 0;
                }
                return enabled == 1;
            }

            uint16_t record(size_t size)
            {
                void* frames[frame_count];
                ULONG hash        = 0;
                USHORT frame_used = RtlCaptureStackBackTrace(2, frame_count, frames, &hash);

                AcquireSRWLockExclusive(&lock);
                uint32_t index = hash % slot_count;
                for (uint32_t probe = 0; probe < slot_count; probe++, index = (index + 1) % slot_count)
                {
                    site& slot = slots[index];
                    if (slot.frame_used == 0)
                    {
                        if (slots_used >= slot_count - 1)
                        {
                            break;
                        }
                        slots_used++;
                        slot.hash       = hash;
                        slot.frame_used = frame_used;
                        memcpy(slot.frames, frames, frame_used * sizeof(void*));
                    }
                    if (slot.hash == hash && slot.frame_used == frame_used && memcmp(slot.frames, frames, frame_used * sizeof(void*)) == 0)
                    {
                        slot.bytes       += static_cast<int64_t>(size);
                        slot.allocations += 1;
                        ReleaseSRWLockExclusive(&lock);
                        return static_cast<uint16_t>(index + 1);
                    }
                }
                ReleaseSRWLockExclusive(&lock);
                return 0;
            }

            void release(uint16_t site_id, size_t size)
            {
                AcquireSRWLockExclusive(&lock);
                slots[site_id - 1].bytes       -= static_cast<int64_t>(size);
                slots[site_id - 1].allocations -= 1;
                ReleaseSRWLockExclusive(&lock);
            }
        }
#endif

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
            {
                return 0;
            }

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
            {
                return nullptr;
            }

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
            {
                return false;
            }

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
            {
                return nullptr;
            }

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
#if defined(_WIN32)
            header->site   = (size >= census::min_size && census::is_enabled()) ? census::record(size) : 0;
#else
            header->site   = 0;
#endif

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
        }

        // perform the actual free (bypassing cache)
        void free_internal(void* ptr)
        {
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

#if defined(_WIN32)
            if (header->site != 0)
            {
                census::release(header->site, size);
            }
#endif

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
            free(raw);
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
                allocation_header* header = reinterpret_cast<allocation_header*>(static_cast<char*>(cached) - header_size);
                
                // re-activate the allocation
                header->magic = allocation_magic_active;
                header->tag   = tag;

                // update tag counter (size is already counted from original allocation)
                bytes_by_tag[static_cast<size_t>(tag)].fetch_add(header->size, memory_order_relaxed);

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
        {
            return;
        }

        const size_t header_size = sizeof(allocation_header);
        allocation_header* header = reinterpret_cast<allocation_header*>(static_cast<char*>(ptr) - header_size);

        // validate before accessing other fields
        if (header->magic == allocation_magic_freed)
        {
            SP_LOG_ERROR("Double-free detected at address %p", ptr);
            SP_ASSERT(false && "double-free detected");
            return;
        }
        if (header->magic != allocation_magic_active)
        {
            SP_LOG_ERROR("Memory corruption detected at address %p (magic: 0x%08X)", ptr, header->magic);
            SP_ASSERT(false && "memory corruption detected");
            return;
        }

        size_t size = header->size;

        // update tag counter before potential caching
        bytes_by_tag[static_cast<size_t>(header->tag)].fetch_sub(size, memory_order_relaxed);

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
        {
            return 0.0f;
        }
        return static_cast<float>(bytes_by_tag[index].load(memory_order_relaxed)) / (1024.0f * 1024.0f);
    }

    void Allocator::LogLargestAllocationSites(const char* label, uint32_t count)
    {
#if defined(_WIN32)
        if (!census::is_enabled())
        {
            return;
        }

        constexpr uint32_t max_report = 64;
        count = min(count, max_report);
        census::site top[max_report] = {};
        uint32_t top_used             = 0;
        int64_t tracked_bytes         = 0;
        AcquireSRWLockShared(&census::lock);
        for (const census::site& slot : census::slots)
        {
            if (slot.frame_used == 0 || slot.bytes <= 0)
            {
                continue;
            }
            tracked_bytes += slot.bytes;
            uint32_t position = top_used;
            while (position > 0 && top[position - 1].bytes < slot.bytes)
            {
                if (position < count)
                {
                    top[position] = top[position - 1];
                }
                position--;
            }
            if (position < count)
            {
                top[position] = slot;
                top_used      = min(top_used + 1, count);
            }
        }
        ReleaseSRWLockShared(&census::lock);

        HANDLE process = GetCurrentProcess();
        static bool symbols_ready = false;
        if (!symbols_ready)
        {
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            symbols_ready = SymInitialize(process, nullptr, TRUE) == TRUE;
        }

        constexpr double mb = 1024.0 * 1024.0;
        SP_LOG_INFO("Heap census (%s): %.0f MB live in allocations of %zu KB or more, %.0f MB total heap", label, tracked_bytes / mb, census::min_size / 1024, GetMemoryAllocatedMb());
        for (uint32_t i = 0; i < top_used; i++)
        {
            char line[2048] = {};
            size_t length   = 0;
            uint32_t shown  = 0;
            for (USHORT f = 0; f < top[i].frame_used && shown < 5; f++)
            {
                char symbol_buffer[sizeof(SYMBOL_INFO) + 256] = {};
                SYMBOL_INFO* symbol  = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
                symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                symbol->MaxNameLen   = 255;
                DWORD64 displacement = 0;
                const DWORD64 address = reinterpret_cast<DWORD64>(top[i].frames[f]);
                if (!SymFromAddr(process, address, &displacement, symbol))
                {
                    continue;
                }
                const char* name = symbol->Name;
                if (strstr(name, "std::") || strstr(name, "operator new") || strstr(name, "Allocator::") || strstr(name, "_Allocate") || strstr(name, "allocate"))
                {
                    continue;
                }
                IMAGEHLP_LINE64 source_line = {};
                source_line.SizeOfStruct    = sizeof(IMAGEHLP_LINE64);
                DWORD line_displacement     = 0;
                if (SymGetLineFromAddr64(process, address, &line_displacement, &source_line))
                {
                    const char* file = strrchr(source_line.FileName, '\\');
                    length += snprintf(line + length, sizeof(line) - length, "%s%s (%s:%lu)", shown ? " <- " : "", name, file ? file + 1 : source_line.FileName, source_line.LineNumber);
                }
                else
                {
                    length += snprintf(line + length, sizeof(line) - length, "%s%s", shown ? " <- " : "", name);
                }
                shown++;
                if (length >= sizeof(line) - 1)
                {
                    break;
                }
            }
            SP_LOG_INFO("Heap census (%s): %8.1f MB in %lld allocations: %s", label, top[i].bytes / mb, top[i].allocations, line);
        }
#endif
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
        {
            return "Unknown";
        }
        return tag_names[index];
    }
}
