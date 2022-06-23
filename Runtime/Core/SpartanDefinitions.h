#pragma once

// TODO: detect clang and add clang specific directives

// Version
#define sp_version "v0.33"

#if defined(__clang__)
    #pragma warn "SP_OPTIMISE and SP_WARNINGS are not implemented for clang"
    
    // Class
    #define SPARTAN_CLASS
    #if SPARTAN_RUNTIME_SHARED == 1
        #ifdef SPARTAN_RUNTIME
            #define SPARTAN_CLASS __attribute__((visibility("default")))
        #else
            #define SPARTAN_CLASS 
        #endif
    #endif

    // Optimisation
    #define SP_OPTIMISE_OFF _Pragma("clang optimize off")
    #define SP_OPTIMISE_ON _Pragma("clang optimize on")

    // Warnings
    #define SP_WARNINGS_OFF _Pragma("clang diagnostic push") \
                            _Pragma("clang diagnostic ignored \"-Wall -Wpedantic\"")
    #define SP_WARNINGS_ON _Pragma("clang diagnostic pop")
#elif defined(__GNUC__) || defined(__GNUG__)

    #include <signal.h>

    // Class
    #define SPARTAN_CLASS
    #if SPARTAN_RUNTIME_SHARED == 1
        #ifdef SPARTAN_RUNTIME
            #define SPARTAN_CLASS __attribute__((visibility("default")))
        #else
            #define SPARTAN_CLASS 
        #endif
    #endif

    // Optimisation
    #define SP_OPTIMISE_OFF _Pragma("GCC optimize(\"O2\"")
    #define SP_OPTIMISE_ON _Pragma("GCC optimize(\"O0\"")

    // Warnings
    #define SP_WARNINGS_OFF _Pragma("GCC diagnostic push") \
                            _Pragma("GCC diagnostic ignored \"-Wall -Wpedantic\"")
    #define SP_WARNINGS_ON _Pragma("GCC diagnostic pop")

    // Debug break
    #define SP_DEBUG_BREAK() raise(SIGTRAP)
#elif defined(_MSC_VER)
    // Class
    #define SPARTAN_CLASS
    #if SPARTAN_RUNTIME_SHARED == 1
        #ifdef SPARTAN_RUNTIME
            #define SPARTAN_CLASS __declspec(dllexport)
        #else
            #define SPARTAN_CLASS __declspec(dllimport)
        #endif
    #endif

    // Optimisation
    #define SP_OPTIMISE_OFF __pragma(optimize("", off))
    #define SP_OPTIMISE_ON __pragma(optimize("", on))

    // Warnings
    #define SP_WARNINGS_OFF __pragma(warning(push, 0))
    #define SP_WARNINGS_ON __pragma(warning(pop))

    // Debug break
    #define SP_DEBUG_BREAK() __debugbreak()
#else
    #pragma warn "Unkown compiler/platform."
#endif


//= Assert ==========================================================================
// On debug mode, the assert will have the default behaviour.
// On release mode, the assert will write the error to a file and then break.
#ifdef DEBUG
#define SP_ASSERT(expression) assert(expression)
#else
#define SP_ASSERT(expression)           \
if (!(##expression))                    \
{                                       \
    Spartan::Log::m_log_to_file = true; \
    LOG_ERROR(#expression);             \
    SP_DEBUG_BREAK();                   \
}
#endif

// An assert which will can print a text message
#define SP_ASSERT_MSG(expression, text_message) SP_ASSERT(expression && text_message)
//===================================================================================

// Safe pointer delete
template<typename T>
constexpr void sp_delete(T* ptr)
{
    if (ptr)
    {
        delete ptr;
        ptr = nullptr;
    }
}

// Windows - Avoid conflicts with numeric limit min/max
#if defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#ifdef _MSC_VER
//= DISABLED WARNINGS ==============================================================================================================================
// identifier' : class 'type' needs to have dll-interface to be used by clients of class 'type2'
#pragma warning(disable: 4251) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-2-c4275?view=vs-2019
// non � DLL-interface classkey 'identifier' used as base for DLL-interface classkey 'identifier'
#pragma warning(disable: 4275) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-2-c4275?view=vs-2019
// no definition for inline function 'function'
#pragma warning(disable: 4506) // https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-1-c4506?view=vs-2017
//==================================================================================================================================================
#endif
