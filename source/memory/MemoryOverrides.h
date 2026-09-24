/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#ifdef __linux__
#include <cstddef>
#include <new>
#endif
// global
void* operator new(size_t size);
void operator delete(void* ptr) noexcept;

// for arrays
void* operator new[](size_t size);
void operator delete[](void* ptr) noexcept;

// sized delete (C++14+)
void operator delete(void* ptr, size_t size) noexcept;
void operator delete[](void* ptr, size_t size) noexcept;

// aligned new/delete (C++17+)
void* operator new(size_t size, std::align_val_t alignment);
void operator delete(void* ptr, std::align_val_t alignment) noexcept;
void* operator new[](size_t size, std::align_val_t alignment);
void operator delete[](void* ptr, std::align_val_t alignment) noexcept;

// sized + aligned delete (C++17+)
void operator delete(void* ptr, size_t size, std::align_val_t alignment) noexcept;
void operator delete[](void* ptr, size_t size, std::align_val_t alignment) noexcept;
