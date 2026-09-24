/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <cstddef>
#include <memory_resource>
#include <new>

namespace spartan
{
    // Stable addresses, with equally sized objects allocated from reusable chunks.
    // A separate resource per tag keeps entity/component storage apart from strings,
    // reflection attributes and other allocations made by their constructors.
    // Synchronization is confined to allocation/free, never the frame's read path.
    template<class Tag>
    class PooledObject
    {
    public:
        static void* operator new(std::size_t size)
        {
            return Resource().allocate(size, alignof(std::max_align_t));
        }

        static void operator delete(void* object, std::size_t size) noexcept
        {
            Resource().deallocate(object, size, alignof(std::max_align_t));
        }

        static void* operator new(std::size_t size, std::align_val_t alignment)
        {
            return Resource().allocate(size, static_cast<std::size_t>(alignment));
        }

        static void operator delete(void* object, std::size_t size, std::align_val_t alignment) noexcept
        {
            Resource().deallocate(object, size, static_cast<std::size_t>(alignment));
        }

    private:
        static std::pmr::synchronized_pool_resource& Resource()
        {
            static std::pmr::synchronized_pool_resource resource({64, 65536});
            return resource;
        }
    };
}
