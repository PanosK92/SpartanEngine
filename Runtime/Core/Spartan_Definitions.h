#pragma once

// Version
constexpr char* sp_version = "v0.32 WIP";

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
    constexpr void sp_ptr_delete(T*& ptr)
    {
        if (ptr)
        {
            delete ptr;
            ptr = nullptr;
        }
    }
}

// Optimisation
#define SP_OPTIMISE_OFF __pragma(optimize("", off))
#define SP_OPTIMISE_ON __pragma(optimize("", on))

// Warnings
#define SP_WARNINGS_OFF __pragma(warning(push, 0))
#define SP_WARNINGS_ON __pragma(warning(pop))

// Assert
#define SP_ASSERT(expression) assert(expression)

// Platform 
//#define API_GRAPHICS_D3D11    -> Defined by solution generation script
//#define API_GRAPHICS_D3D12    -> Defined by solution generation script
//#define API_GRAPHICS_VULKAN   -> Defined by solution generation script
#define API_INPUT_WINDOWS //    -> Explicitly defined for now

// Fix windows macros
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

//= DISABLED WARNINGS ==============================================================================================================================
// identifier' : class 'type' needs to have dll-interface to be used by clients of class 'type2'
#pragma warning(disable: 4251) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-2-c4275?view=vs-2019
// non – DLL-interface classkey 'identifier' used as base for DLL-interface classkey 'identifier'
#pragma warning(disable: 4275) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-2-c4275?view=vs-2019
// no definition for inline function 'function'
#pragma warning(disable: 4506) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-1-c4506?view=vs-2017
//==================================================================================================================================================
