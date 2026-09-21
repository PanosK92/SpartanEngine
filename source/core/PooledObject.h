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
