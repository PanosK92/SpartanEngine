/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============
#include "pch.h"
#include "MemoryOverrides.h"
#include "Allocator.h"
//==========================

void* operator new(size_t size)
{
    void* ptr = spartan::Allocator::Allocate(size);
    if (!ptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete(void* ptr) noexcept
{
    spartan::Allocator::Free(ptr);
}

void* operator new[](size_t size)
{
    void* ptr = spartan::Allocator::Allocate(size);
    if (!ptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete[](void* ptr) noexcept
{
    spartan::Allocator::Free(ptr);
}

// sized delete (C++14+)
void operator delete(void* ptr, size_t) noexcept
{
    spartan::Allocator::Free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept
{
    spartan::Allocator::Free(ptr);
}

// aligned new/delete (C++17+)
void* operator new(size_t size, std::align_val_t alignment)
{
    void* ptr = spartan::Allocator::Allocate(size, static_cast<size_t>(alignment));
    if (!ptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete(void* ptr, std::align_val_t) noexcept
{
    spartan::Allocator::Free(ptr);
}

void* operator new[](size_t size, std::align_val_t alignment)
{
    void* ptr = spartan::Allocator::Allocate(size, static_cast<size_t>(alignment));
    if (!ptr)
    {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete[](void* ptr, std::align_val_t) noexcept
{
    spartan::Allocator::Free(ptr);
}

// sized + aligned delete (C++17+)
void operator delete(void* ptr, size_t, std::align_val_t) noexcept
{
    spartan::Allocator::Free(ptr);
}

void operator delete[](void* ptr, size_t, std::align_val_t) noexcept
{
    spartan::Allocator::Free(ptr);
}
