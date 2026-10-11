/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/


//= INCLUDES ====================
#include "pch.h"
#include "Allocator.h"
#include "FrameAllocator.h"
#include <cstring>
#include <mimalloc.h>
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

        // header stores allocation metadata, it sits right before the user pointer
        struct allocation_header
        {
            uint64_t  size;    // requested size
            uint32_t  magic;   // magic number for corruption/double-free detection
            uint16_t  site;    // census slot + 1, 0 when untracked
            MemoryTag tag;     // memory tag for tracking
            uint8_t   padding;
        };

        // the header size is also the minimum alignment, operator new must return 16-byte aligned memory on x64
        constexpr size_t header_size       = sizeof(allocation_header);
        constexpr size_t minimum_alignment = 16;
        static_assert(header_size == minimum_alignment, "the header must keep the user pointer aligned to the minimum alignment");
        static_assert(minimum_alignment >= alignof(max_align_t), "the minimum alignment must satisfy every fundamental type");

        // counters are spread over cache line sized slots so threads don't contend on one line
        // a block can be freed on a different thread than the one that allocated it, so a single slot can go negative, only the sum is meaningful
        constexpr uint32_t stat_slot_count = 64;
        constexpr size_t   tag_count       = static_cast<size_t>(MemoryTag::Count);

        struct alignas(64) stat_slot
        {
            atomic<int64_t> bytes             = 0;
            atomic<int64_t> count             = 0;
            atomic<int64_t> allocations_total = 0; // never decremented, used for the per frame rate
            atomic<int64_t> bytes_by_tag[tag_count] = {};
        };

        stat_slot stat_slots[stat_slot_count];
        atomic<uint32_t> stat_slot_next      = 0;
        thread_local uint32_t tl_stat_slot   = UINT32_MAX;
        atomic<int64_t> bytes_allocated_peak = 0;
        uint64_t allocations_total_last_tick = 0;
        uint64_t allocations_last_frame      = 0;

        // mimalloc only returns a thread's free pages to the os when that thread collects, so a request is picked up by every thread on its next allocation
        atomic<uint32_t> collect_request = 0;
        thread_local uint32_t tl_collect_seen = 0;

        stat_slot& get_stat_slot()
        {
            if (tl_stat_slot == UINT32_MAX)
            {
                tl_stat_slot = stat_slot_next.fetch_add(1, memory_order_relaxed) % stat_slot_count;
            }
            return stat_slots[tl_stat_slot];
        }

        int64_t sum_bytes_allocated()
        {
            int64_t total = 0;
            for (const stat_slot& slot : stat_slots)
            {
                total += slot.bytes.load(memory_order_relaxed);
            }
            return total;
        }

        // the peak is sampled whenever the total is read (at least once per frame from Tick), short spikes within a frame are not seen
        void update_peak(int64_t current)
        {
            int64_t peak = bytes_allocated_peak.load(memory_order_relaxed);
            while (current > peak && !bytes_allocated_peak.compare_exchange_weak(peak, current, memory_order_relaxed, memory_order_relaxed))
            {
                // peak is updated by compare_exchange_weak on failure
            }
        }

#if defined(_WIN32)
        // call site census, it can't allocate since it runs inside the allocator
        namespace census
        {
            constexpr uint32_t frame_count = 20;
            constexpr uint32_t slot_count  = 8192;

            struct site
            {
                ULONG  hash;
                void*  frames[frame_count];
                USHORT frame_used;
                int64_t bytes;
                int64_t allocations;
                int64_t allocations_total; // never decremented, reset when the census is started at runtime
            };

            site slots[slot_count] = {};
            uint32_t slots_used    = 0;
            SRWLOCK lock           = SRWLOCK_INIT;
            atomic<int> enabled    = -1;
            atomic<size_t> min_size_bytes = 16 * 1024;

            bool is_enabled()
            {
                int state = enabled.load(memory_order_relaxed);
                if (state < 0)
                {
                    char value[4] = {};
                    state = (GetEnvironmentVariableA("SPARTAN_HEAP_CENSUS", value, sizeof(value)) > 0 && value[0] == '1') ? 1 : 0;
                    enabled.store(state, memory_order_relaxed);
                }
                return state == 1;
            }

            size_t min_size()
            {
                return min_size_bytes.load(memory_order_relaxed);
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
                        slot.bytes             += static_cast<int64_t>(size);
                        slot.allocations       += 1;
                        slot.allocations_total += 1;
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

            // copies the count highest ranked sites by key into top, sorted descending
            uint32_t collect_top(site* top, uint32_t count, int64_t site::* key, int64_t* key_sum)
            {
                uint32_t top_used = 0;
                int64_t sum       = 0;
                AcquireSRWLockShared(&lock);
                for (const site& slot : slots)
                {
                    if (slot.frame_used == 0 || slot.*key <= 0)
                    {
                        continue;
                    }
                    sum += slot.*key;
                    uint32_t position = top_used;
                    while (position > 0 && top[position - 1].*key < slot.*key)
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
                ReleaseSRWLockShared(&lock);

                if (key_sum)
                {
                    *key_sum = sum;
                }
                return top_used;
            }

            // writes the first few frames outside the allocator and the standard library, dbghelp is single threaded so call it from one thread
            void describe(const site& slot, char* line, size_t line_size)
            {
                HANDLE process = GetCurrentProcess();
                static bool symbols_ready = false;
                if (!symbols_ready)
                {
                    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
                    symbols_ready = SymInitialize(process, nullptr, TRUE) == TRUE;
                }

                size_t length  = 0;
                uint32_t shown = 0;
                for (USHORT f = 0; f < slot.frame_used && shown < 5; f++)
                {
                    char symbol_buffer[sizeof(SYMBOL_INFO) + 256] = {};
                    SYMBOL_INFO* symbol  = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
                    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                    symbol->MaxNameLen   = 255;
                    DWORD64 displacement = 0;
                    const DWORD64 address = reinterpret_cast<DWORD64>(slot.frames[f]);
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
                        length += snprintf(line + length, line_size - length, "%s%s (%s:%lu)", shown ? " <- " : "", name, file ? file + 1 : source_line.FileName, source_line.LineNumber);
                    }
                    else
                    {
                        length += snprintf(line + length, line_size - length, "%s%s", shown ? " <- " : "", name);
                    }
                    shown++;
                    if (length >= line_size - 1)
                    {
                        break;
                    }
                }
            }
        }
#endif

    }

    void* Allocator::Allocate(size_t size, size_t alignment, MemoryTag tag)
    {
        if (size > SIZE_MAX - header_size)
        {
            return nullptr;
        }

        const uint32_t collect = collect_request.load(memory_order_relaxed);
        if (tl_collect_seen != collect)
        {
            tl_collect_seen = collect;
            mi_collect(false);
        }

        // mimalloc returns 16-byte aligned blocks for every size we request (always >= 16), so only over-aligned requests take the slower path
        alignment = max(alignment, minimum_alignment);
        void* raw = alignment == minimum_alignment ? mi_malloc(size + header_size) : mi_malloc_aligned_at(size + header_size, alignment, header_size);
        if (!raw)
        {
            return nullptr;
        }

        void* user_ptr            = static_cast<char*>(raw) + header_size;
        allocation_header* header = static_cast<allocation_header*>(raw);
        header->size              = size;
        header->magic             = allocation_magic_active;
        header->tag               = tag;
        header->padding           = 0;
#if defined(_WIN32)
        header->site              = (size >= census::min_size() && census::is_enabled()) ? census::record(size) : 0;
#else
        header->site              = 0;
#endif

#if defined(_DEBUG) || defined(DEBUG)
        // poison allocated memory in debug builds to catch uninitialized reads
        memset(user_ptr, poison_allocated, size);
#endif

        stat_slot& slot = get_stat_slot();
        slot.bytes.fetch_add(static_cast<int64_t>(size), memory_order_relaxed);
        slot.count.fetch_add(1, memory_order_relaxed);
        slot.allocations_total.fetch_add(1, memory_order_relaxed);
        slot.bytes_by_tag[static_cast<size_t>(tag)].fetch_add(static_cast<int64_t>(size), memory_order_relaxed);

        return user_ptr;
    }

    void Allocator::Free(void* ptr)
    {
        if (!ptr)
        {
            return;
        }

        allocation_header* header = reinterpret_cast<allocation_header*>(static_cast<char*>(ptr) - header_size);

        // best effort, a freed block can be reused by mimalloc before a second free reaches it
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

        const size_t size   = static_cast<size_t>(header->size);
        const MemoryTag tag = header->tag;

#if defined(_WIN32)
        if (header->site != 0)
        {
            census::release(header->site, size);
        }
#endif

        header->magic = allocation_magic_freed;

#if defined(_DEBUG) || defined(DEBUG)
        // poison freed memory in debug builds to catch use-after-free
        memset(ptr, poison_freed, size);
#endif

        stat_slot& slot = get_stat_slot();
        slot.bytes.fetch_sub(static_cast<int64_t>(size), memory_order_relaxed);
        slot.count.fetch_sub(1, memory_order_relaxed);
        slot.bytes_by_tag[static_cast<size_t>(tag)].fetch_sub(static_cast<int64_t>(size), memory_order_relaxed);

        mi_free(header);
    }

    void Allocator::Tick()
    {
        static bool has_warned                    = false; // only warn once per threshold crossing
        constexpr float warning_threshold_percent = 90.0f; // 90%

        FrameAllocator::Tick();
        update_peak(sum_bytes_allocated());

        uint64_t allocations_total = 0;
        for (const stat_slot& slot : stat_slots)
        {
            allocations_total += static_cast<uint64_t>(slot.allocations_total.load(memory_order_relaxed));
        }
        allocations_last_frame      = allocations_total - allocations_total_last_tick;
        allocations_total_last_tick = allocations_total;
    
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
        const int64_t total = sum_bytes_allocated();
        update_peak(total);
        return static_cast<float>(max<int64_t>(total, 0)) / (1024.0f * 1024.0f);
    }

    uint64_t Allocator::GetAllocationCount()
    {
        int64_t total = 0;
        for (const stat_slot& slot : stat_slots)
        {
            total += slot.count.load(memory_order_relaxed);
        }
        return static_cast<uint64_t>(max<int64_t>(total, 0));
    }

    uint64_t Allocator::GetAllocationsLastFrame()
    {
        return allocations_last_frame;
    }

    void Allocator::Collect()
    {
        tl_collect_seen = collect_request.fetch_add(1, memory_order_relaxed) + 1;
        mi_collect(true);
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
        update_peak(sum_bytes_allocated());
        return static_cast<float>(bytes_allocated_peak.load(memory_order_relaxed)) / (1024.0f * 1024.0f);
    }

    float Allocator::GetMemoryAllocatedByTagMb(MemoryTag tag)
    {
        size_t index = static_cast<size_t>(tag);
        if (index >= tag_count)
        {
            return 0.0f;
        }

        int64_t total = 0;
        for (const stat_slot& slot : stat_slots)
        {
            total += slot.bytes_by_tag[index].load(memory_order_relaxed);
        }
        return static_cast<float>(max<int64_t>(total, 0)) / (1024.0f * 1024.0f);
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
        int64_t tracked_bytes         = 0;
        const uint32_t top_used       = census::collect_top(top, count, &census::site::bytes, &tracked_bytes);

        constexpr double mb = 1024.0 * 1024.0;
        SP_LOG_INFO("Heap census (%s): %.0f MB live in allocations of %zu KB or more, %.0f MB total heap", label, tracked_bytes / mb, census::min_size() / 1024, GetMemoryAllocatedMb());
        for (uint32_t i = 0; i < top_used; i++)
        {
            char line[2048] = {};
            census::describe(top[i], line, sizeof(line));
            SP_LOG_INFO("Heap census (%s): %8.1f MB in %lld allocations: %s", label, top[i].bytes / mb, top[i].allocations, line);
        }
#endif
    }

    void Allocator::SetAllocationCensus(bool enabled, size_t min_size)
    {
#if defined(_WIN32)
        if (enabled)
        {
            AcquireSRWLockExclusive(&census::lock);
            for (census::site& slot : census::slots)
            {
                slot.allocations_total = 0;
            }
            ReleaseSRWLockExclusive(&census::lock);
            census::min_size_bytes.store(min_size, memory_order_relaxed);
        }
        census::enabled.store(enabled ? 1 : 0, memory_order_relaxed);
#endif
    }

    bool Allocator::IsAllocationCensusEnabled()
    {
#if defined(_WIN32)
        return census::is_enabled();
#else
        return false;
#endif
    }

    vector<AllocationSite> Allocator::GetBusiestAllocationSites(uint32_t count)
    {
        vector<AllocationSite> sites;
#if defined(_WIN32)
        constexpr uint32_t max_report = 64;
        count = min(count, max_report);
        census::site top[max_report] = {};
        const uint32_t top_used       = census::collect_top(top, count, &census::site::allocations_total, nullptr);

        sites.reserve(top_used);
        for (uint32_t i = 0; i < top_used; i++)
        {
            char line[2048] = {};
            census::describe(top[i], line, sizeof(line));

            AllocationSite& site    = sites.emplace_back();
            site.call_stack         = line;
            site.allocations_total  = static_cast<uint64_t>(top[i].allocations_total);
            site.live_allocations   = top[i].allocations;
            site.live_bytes         = top[i].bytes;
        }
#endif
        return sites;
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
