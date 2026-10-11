/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ============
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <vector>
//=======================

namespace spartan
{
    // per thread bump allocator for scratch memory that dies with the frame
    // memory allocated during frame n stays valid until the end of frame n + 1, then it is reused without being freed
    // never keep frame memory in anything that outlives that, e.g. members, caches or jobs that span frames
    class FrameAllocator
    {
    public:
        static void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

        // memory resource for std::pmr containers, it allocates from the calling thread's arena so containers may grow on any thread
        static std::pmr::memory_resource* GetResource();

        // advances the frame, called once per frame by Allocator::Tick()
        static void Tick();

        // bytes handed out during the last completed frame, over all threads
        static std::size_t GetBytesLastFrame();
    };

    // vector that allocates from the frame allocator, construct it, fill it and drop it within the frame
    template<typename T>
    using frame_vector = std::pmr::vector<T>;

    template<typename T>
    frame_vector<T> make_frame_vector(std::size_t reserve = 0)
    {
        frame_vector<T> vector(FrameAllocator::GetResource());
        vector.reserve(reserve);
        return vector;
    }
}
