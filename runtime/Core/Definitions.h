#pragma once

// TODO: detect clang and add clang specific directives

// Version
constexpr char sp_name[]          = "Spartan";
constexpr int sp_version_major    = 0;
constexpr int sp_version_minor    = 3;
constexpr int sp_version_revision = 3;

#if defined(__clang__)
    #pragma warn "SP_OPTIMISE and SP_WARNINGS are not implemented for clang"

    // Class
    #define SP_CLASS
    #if SPARTAN_RUNTIME_SHARED == 1
        #ifdef SPARTAN_RUNTIME
            #define SP_CLASS __attribute__((visibility("default")))
        #else
            #define SP_CLASS 
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
    #define SP_CLASS
    #if SPARTAN_RUNTIME_SHARED == 1
        #ifdef SPARTAN_RUNTIME
            #define SP_CLASS __attribute__((visibility("default")))
        #else
            #define SP_CLASS 
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
    #define SP_CLASS
    #if SPARTAN_RUNTIME_SHARED == 1
        #ifdef SPARTAN_RUNTIME
            #define SP_CLASS __declspec(dllexport)
        #else
            #define SP_CLASS __declspec(dllimport)
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

//= ERROR WINDOW ============================================================
// An error window that can display text and terminate the program

#define WIDE_STR_HELPER(x) L ## x
#define WIDE_STR(x) WIDE_STR_HELPER(x)

#if defined(_MSC_VER)
#define SP_ERROR_WINDOW(text_message)                                       \
{                                                                           \
    MessageBeep(MB_ICONERROR);                                              \
    HWND hwnd = GetConsoleWindow();                                         \
    MessageBox(hwnd, WIDE_STR(text_message), L"Error", MB_OK | MB_TOPMOST); \
    SP_DEBUG_BREAK();                                                       \
}
#else
#pragma warn "SP_ERROR_WINDOW not implemented for this compiler/platform"
#endif
//===========================================================================

//= ASSERT ==========================================================================
// On debug mode, the assert will have the default behaviour.
// On release mode, the assert will write the error to a file and then break.
#include <cassert>
#ifdef DEBUG
#define SP_ASSERT(expression) assert(expression)
#else
#define SP_ASSERT(expression)         \
if (!(##expression))                  \
{                                     \
    Spartan::Log::SetLogToFile(true); \
    SP_LOG_ERROR(#expression);        \
    SP_DEBUG_BREAK();                 \
}
#endif

// An assert which can print a text message
#define SP_ASSERT_MSG(expression, text_message) \
SP_ASSERT(expression && text_message)

// A static assert
#define SP_ASSERT_STATIC_IS_TRIVIALLY_COPYABLE(T) \
static_assert(std::is_trivially_copyable_v<T>, "Type is not trivially copyable")
//===================================================================================

#if defined(_MSC_VER)
//= DISABLE CERTAIN WARNINGS ========================================================================================
#pragma warning(disable: 4251) 
// 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
// https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-1-c4251?view=msvc-170

#pragma warning(disable: 4275) 
// non - DLL-interface class 'class_1' used as base for DLL-interface class 'class_2'
// https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-2-c4275?view=msvc-170

#pragma warning(disable: 4506) 
// no definition for inline function 'function'
// https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-1-c4506?view=msvc-170

#pragma warning(disable: 4996) 
// 'sprintf': This function or variable may be unsafe. Consider using sprintf_s instead.
// https://docs.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-3-c4996?view=msvc-170

#pragma warning(disable: 26110) 
// Caller failing to hold lock <lock> before calling function <func>
// https://docs.microsoft.com/en-us/cpp/code-quality/c26110?view=msvc-170
//===================================================================================================================
#endif

// Windows - Avoid conflicts with numeric limit min/max
#if defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
