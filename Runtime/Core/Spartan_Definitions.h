#pragma once

// Version
constexpr auto engine_version = "v0.32 WIP";

// Class
#define SPARTAN_CLASS
#if SPARTAN_RUNTIME_SHARED == 1
#ifdef SPARTAN_RUNTIME
#define SPARTAN_CLASS __declspec(dllexport)
#else
#define SPARTAN_CLASS __declspec(dllimport)
#endif
#endif

// Common functions
namespace Spartan
{
    template <typename T>
    constexpr void safe_delete(T*& ptr)
    {
        if (ptr)
        {
            delete ptr;
            ptr = nullptr;
        }
    }
}

// Windows
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
