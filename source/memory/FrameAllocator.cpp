/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==============
#include "pch.h"
#include "FrameAllocator.h"
#include "Allocator.h"
//=========================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        constexpr size_t chunk_size_default = 256 * 1024;

        atomic<uint64_t> frame_index = 0;
        size_t bytes_last_frame      = 0;

        size_t round_up_pow2(size_t value)
        {
            size_t result = chunk_size_default;
            while (result < value)
            {
                result <<= 1;
            }
            return result;
        }

        struct chunk
        {
            char*  data = nullptr;
            size_t size = 0;
        };

        struct arena
        {
            vector<chunk> chunks;
            size_t chunk_index        = 0;
            size_t offset             = 0;
            atomic<size_t> bytes_used = 0;

            void release()
            {
                for (chunk& c : chunks)
                {
                    Allocator::Free(c.data);
                }
                chunks.clear();
            }

            void add_chunk(size_t size)
            {
                chunk c;
                c.data = static_cast<char*>(Allocator::Allocate(size, 64));
                c.size = size;
                SP_ASSERT(c.data != nullptr);
                chunks.push_back(c);
            }

            // keeps a single chunk sized for what the frame needed, shrinking it again once a spike has passed
            void reset()
            {
                const size_t used     = bytes_used.load(memory_order_relaxed);
                const size_t capacity = chunks.size() == 1 ? chunks[0].size : 0;
                const size_t wanted   = round_up_pow2(used * 2);
                if (chunks.size() > 1 || (capacity > chunk_size_default && used < capacity / 4))
                {
                    release();
                    add_chunk(wanted);
                }

                chunk_index = 0;
                offset      = 0;
                bytes_used.store(0, memory_order_relaxed);
            }

            void* allocate(size_t size, size_t alignment)
            {
                while (true)
                {
                    if (chunk_index < chunks.size())
                    {
                        chunk& c             = chunks[chunk_index];
                        const uintptr_t base = reinterpret_cast<uintptr_t>(c.data);
                        const uintptr_t ptr  = (base + offset + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
                        if (ptr + size <= base + c.size)
                        {
                            offset = ptr + size - base;
                            bytes_used.fetch_add(size, memory_order_relaxed);
                            return reinterpret_cast<void*>(ptr);
                        }

                        chunk_index++;
                        offset = 0;
                        continue;
                    }

                    add_chunk(max(chunk_size_default, round_up_pow2(size + alignment)));
                    chunk_index = chunks.size() - 1;
                    offset      = 0;
                }
            }
        };

        // two arenas, the one for the current frame parity is reset when a thread first allocates in a new frame
        struct thread_arenas;
        mutex registry_mutex;
        vector<thread_arenas*> registry;

        // chunks of exited threads, other threads may still use their memory until the frame lifetime ends
        struct retired_chunk
        {
            chunk    memory;
            uint64_t frame = 0;
        };
        vector<retired_chunk> retired;

        struct thread_arenas
        {
            arena buffers[2];
            atomic<uint64_t> frame = UINT64_MAX;

            thread_arenas()
            {
                lock_guard<mutex> lock(registry_mutex);
                registry.push_back(this);
            }

            ~thread_arenas()
            {
                lock_guard<mutex> lock(registry_mutex);
                registry.erase(remove(registry.begin(), registry.end(), this), registry.end());
                const uint64_t current = frame_index.load(memory_order_relaxed);
                for (arena& buffer : buffers)
                {
                    for (const chunk& c : buffer.chunks)
                    {
                        retired.push_back({ c, current });
                    }
                    buffer.chunks.clear();
                }
            }
        };

        thread_local thread_arenas tl_arenas;

        class frame_resource : public pmr::memory_resource
        {
            void* do_allocate(size_t size, size_t alignment) override
            {
                return FrameAllocator::Allocate(size, alignment);
            }

            void do_deallocate(void*, size_t, size_t) override
            {
                // released in bulk when the frame's arena is reused
            }

            bool do_is_equal(const pmr::memory_resource& other) const noexcept override
            {
                return this == &other;
            }
        };

        frame_resource resource;
    }

    void* FrameAllocator::Allocate(size_t size, size_t alignment)
    {
        SP_ASSERT((alignment & (alignment - 1)) == 0);

        const uint64_t frame = frame_index.load(memory_order_relaxed);
        thread_arenas& arenas = tl_arenas;
        if (arenas.frame.load(memory_order_relaxed) != frame)
        {
            arenas.buffers[frame & 1].reset();
            arenas.frame.store(frame, memory_order_relaxed);
        }

        return arenas.buffers[frame & 1].allocate(max<size_t>(size, 1), max(alignment, alignof(max_align_t)));
    }

    pmr::memory_resource* FrameAllocator::GetResource()
    {
        return &resource;
    }

    void FrameAllocator::Tick()
    {
        const uint64_t frame = frame_index.load(memory_order_relaxed);

        size_t bytes = 0;
        {
            lock_guard<mutex> lock(registry_mutex);
            for (thread_arenas* arenas : registry)
            {
                if (arenas->frame.load(memory_order_relaxed) == frame)
                {
                    bytes += arenas->buffers[frame & 1].bytes_used.load(memory_order_relaxed);
                }
            }

            // memory handed out in frame n is valid until frame n + 1 ends
            erase_if(retired, [frame](const retired_chunk& entry)
            {
                if (entry.frame + 1 < frame)
                {
                    Allocator::Free(entry.memory.data);
                    return true;
                }
                return false;
            });
        }
        bytes_last_frame = bytes;

        frame_index.store(frame + 1, memory_order_relaxed);
    }

    size_t FrameAllocator::GetBytesLastFrame()
    {
        return bytes_last_frame;
    }
}
