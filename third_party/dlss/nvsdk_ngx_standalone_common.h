/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NVSDK_NGX_STANDALONE_COMMON_H
#define NVSDK_NGX_STANDALONE_COMMON_H

#if defined(NVSDK_NGX_HEADER_ONLY)

#include <mutex>
typedef std::recursive_mutex NGXMutex;
typedef std::lock_guard<std::recursive_mutex> NGXLock;

#define NGX_CONCAT_IMPL(A, B) A##B
#define NGX_CONCAT(A, B) NGX_CONCAT_IMPL(A, B)
#define NGX_CONCAT_3_IMPL(A, B, C) A##B##C
#define NGX_CONCAT_3(A, B, C) NGX_CONCAT_3_IMPL(A, B, C)
#define NGX_CONCAT_4_IMPL(A, B, C, D) A##B##C##D
#define NGX_CONCAT_4(A, B, C, D) NGX_CONCAT_4_IMPL(A, B, C, D)
#define NGX_EXPAND(x) x
#define NGX_STR_IMPL(x) #x
#define NGX_STR(x) NGX_STR_IMPL(x)

/**
 * Recursively applies a target macro for each pair of arguments in a supplied list.
 *
 * This chain of macros allows iterating over a variadic list of arguments in pairs.
 * Each macro invokes the target macro X with the current pair (a, b), and then passes
 * the remaining arguments to the next macro down the chain.
 *
 * Supports processing up to 40 total arguments (20 pairs).
 */
#define NGX_FE_2(X, a, b) X(a, b)
#define NGX_FE_4(X, a, b, ...) X(a, b) NGX_FE_2(X, __VA_ARGS__)
#define NGX_FE_6(X, a, b, ...) X(a, b) NGX_FE_4(X, __VA_ARGS__)
#define NGX_FE_8(X, a, b, ...) X(a, b) NGX_FE_6(X, __VA_ARGS__)
#define NGX_FE_10(X, a, b, ...) X(a, b) NGX_FE_8(X, __VA_ARGS__)
#define NGX_FE_12(X, a, b, ...) X(a, b) NGX_FE_10(X, __VA_ARGS__)
#define NGX_FE_14(X, a, b, ...) X(a, b) NGX_FE_12(X, __VA_ARGS__)
#define NGX_FE_16(X, a, b, ...) X(a, b) NGX_FE_14(X, __VA_ARGS__)
#define NGX_FE_18(X, a, b, ...) X(a, b) NGX_FE_16(X, __VA_ARGS__)
#define NGX_FE_20(X, a, b, ...) X(a, b) NGX_FE_18(X, __VA_ARGS__)
#define NGX_FE_22(X, a, b, ...) X(a, b) NGX_FE_20(X, __VA_ARGS__)
#define NGX_FE_24(X, a, b, ...) X(a, b) NGX_FE_22(X, __VA_ARGS__)
#define NGX_FE_26(X, a, b, ...) X(a, b) NGX_FE_24(X, __VA_ARGS__)
#define NGX_FE_28(X, a, b, ...) X(a, b) NGX_FE_26(X, __VA_ARGS__)
#define NGX_FE_30(X, a, b, ...) X(a, b) NGX_FE_28(X, __VA_ARGS__)
#define NGX_FE_32(X, a, b, ...) X(a, b) NGX_FE_30(X, __VA_ARGS__)
#define NGX_FE_34(X, a, b, ...) X(a, b) NGX_FE_32(X, __VA_ARGS__)
#define NGX_FE_36(X, a, b, ...) X(a, b) NGX_FE_34(X, __VA_ARGS__)
#define NGX_FE_38(X, a, b, ...) X(a, b) NGX_FE_36(X, __VA_ARGS__)
#define NGX_FE_40(X, a, b, ...) X(a, b) NGX_FE_38(X, __VA_ARGS__)

/**
 * Internal helper to extract the argument count passed to a macro
 */
#define NGX_FE_NARGS_I(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10,          \
                       _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, \
                       _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, \
                       _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, N, ...) N

/**
 * Returns the number of arguments passed in a variadic list up to a maximum of 40 arguments
 */
#define NGX_FE_NARGS(...) NGX_EXPAND(NGX_FE_NARGS_I(__VA_ARGS__,                            \
                                                    40, 39, 38, 37, 36, 35, 34, 33, 32, 31, \
                                                    30, 29, 28, 27, 26, 25, 24, 23, 22, 21, \
                                                    20, 19, 18, 17, 16, 15, 14, 13, 12, 11, \
                                                    10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))

#define NGX_FOREACH_N(N, X, ...) NGX_CONCAT(NGX_FE_, N)(X, __VA_ARGS__)

/**
 * Iterates over a variadic list of arguments in pairs, applying macro X to each pair.
 */
#define NGX_FOREACH(X, ...) NGX_FOREACH_N(NGX_FE_NARGS(__VA_ARGS__), X, __VA_ARGS__)

/**
 * Helper macro to generate default initialiser code for all NGX function pointers passed
 * into NVSDK_NGX_DEFINE_API.
 */
#define NGX_PFNS_DECL(namespace, name) NGX_CONCAT_4(PFN_NVSDK_NGX_, namespace, _, name) name = nullptr;

/**
 * Generates a backend-specific bootstrap code that defines the main struct holding all
 * the function pointers for this backend.
 *
 * The list of functions passed into this macro are tuples of the form (namespace, name).
 *
 * "namespace" is the prefix to apply to this function when named internally. This is
 * typically the name of the backend (e.g. "CUDA" or "D3D12"), but may differ if the function
 * as exported from the SDK is different to the symbol as exported from NGX Core.
 *
 * "name" is the name of the function as exported from NGX Core without any prefixes, 
 * e.g. "Init" or "GetFeatureRequirements".
 * 
 * This requires setting NVSDK_NGX_BACKEND_NAME first to get the overall backend name.
 */
#define NVSDK_NGX_DEFINE_API(is_inited, ...)                                  \
    struct NGX_CONCAT_3(NGX_API_, NVSDK_NGX_BACKEND_NAME, _Lib)               \
    {                                                                         \
        NGX_FOREACH(NGX_PFNS_DECL, __VA_ARGS__)                               \
        bool isInited() const                                                 \
        {                                                                     \
            return is_inited;                                                 \
        }                                                                     \
    };                                                                        \
    struct NGX_CONCAT_3(NGX_Context_, NVSDK_NGX_BACKEND_NAME, _Lib)           \
    {                                                                         \
        NGX_CONCAT_3(NGX_API_, NVSDK_NGX_BACKEND_NAME, _Lib)                  \
        API;                                                                  \
    };                                                                        \
    static NGX_CONCAT_3(NGX_Context_, NVSDK_NGX_BACKEND_NAME, _Lib) GContext; \
    static NGXLibHandle GLib = nullptr;                                       \
    static NGXMutex NGX_CONCAT(g_sdkLibMutex, NVSDK_NGX_BACKEND_NAME);

/**
 * Helper macro to generate initialiser code for all NGX function pointers passed into
 * NVSDK_NGX_DEFINE_INIT_COMMON.
 */
#define NGX_PFNS_GETSYMBOL(namespace, name) \
    GContext.API.name = (NGX_CONCAT_4(PFN_NVSDK_NGX_, namespace, _, name))NGXGetLibrarySymbol(GLib, NGX_STR(NGX_CONCAT_4(NVSDK_NGX_, NVSDK_NGX_BACKEND_NAME, _, name)));

/**
 * Generates a backend-specific Init_Common() function that loads the NGX Core library
 * and assigns the function pointers set up by NVSDK_NGX_DEFINE_API() to the exported
 * functions from NGX Core.
 * 
 * This requires setting NVSDK_NGX_BACKEND_NAME first to get the overall backend name,
 * and takes the same list of functions that NVSDK_NGX_DEFINE_API() does.
 */
#define NVSDK_NGX_DEFINE_INIT_COMMON(...)                                             \
    NVSDK_NGX_Result NGX_CONCAT_3(NVSDK_NGX_, NVSDK_NGX_BACKEND_NAME, _Init_Common()) \
    {                                                                                 \
        if (GContext.API.isInited())                                                  \
        {                                                                             \
            return NVSDK_NGX_Result_Success;                                          \
        }                                                                             \
        GContext = NGX_CONCAT_3(NGX_Context_, NVSDK_NGX_BACKEND_NAME, _Lib());        \
                                                                                      \
        NVSDK_NGX_LUID luid = {0};                                                    \
                                                                                      \
        if (GLib == nullptr)                                                          \
        {                                                                             \
            GLib = NGXLoadCoreLibrary(luid);                                          \
        }                                                                             \
                                                                                      \
        if (GLib)                                                                     \
        {                                                                             \
            NGX_FOREACH(NGX_PFNS_GETSYMBOL, __VA_ARGS__)                              \
        }                                                                             \
                                                                                      \
        if (!GContext.API.isInited())                                                 \
        {                                                                             \
            GContext = NGX_CONCAT_3(NGX_Context_, NVSDK_NGX_BACKEND_NAME, _Lib());    \
            return NVSDK_NGX_Result_FAIL_FeatureNotSupported;                         \
        }                                                                             \
                                                                                      \
        return NVSDK_NGX_Result_Success;                                              \
    }

/**
 * Generates a wrapper for a given API that checks if the SDK is initialised, and if
 * so forwards the API call to the function pointer on NGX Core.
 */
#define NVSDK_NGX_DEFINE_WRAPPER(fn, params, args)                                             \
    NVSDK_NGX_Result NVSDK_CONV NGX_CONCAT_4(NVSDK_NGX_, NVSDK_NGX_BACKEND_NAME, _, fn) params \
    {                                                                                          \
        if (!GContext.API.isInited())                                                          \
            return NVSDK_NGX_Result_FAIL_NotInitialized;                                       \
        if (!GContext.API.fn)                                                                  \
            return NVSDK_NGX_Result_FAIL_OutOfDate;                                            \
        return GContext.API.fn args;                                                           \
    }

#if defined(_WIN32)
  #define NGXThreadLocal __declspec(thread)
#else
  #define NGXThreadLocal __thread
#endif

/**
 * Helper struct to safely manage and dispatch NGX progress callbacks.
 *
 * This structure bridges the gap between the expected internal NGX callback signature 
 * (which uses a bool reference for cancellation) and the user-provided C-style callback 
 * (which expects a pointer). It safely stores the user's callback in thread-local storage 
 * during feature evaluation to ensure thread safety across concurrent API calls.
 */
struct NGXProgressCallbackHelper
{
    static NGXThreadLocal PFN_NVSDK_NGX_ProgressCallback_C tls_OriginalCallback;
    static void s_ReplacementCallback(float InProgress, bool &OutShouldCancel)
    {
        if (tls_OriginalCallback)
        {
            return tls_OriginalCallback(InProgress, &OutShouldCancel);
        }
    }

    template<class FuncType, class CmdListType >
    static NVSDK_NGX_Result EvaluateFeature(FuncType EvaluateFeatureFunc, CmdListType InStream,
                                            const NVSDK_NGX_Handle *InHandle, const NVSDK_NGX_Parameter *InParameters,
                                            PFN_NVSDK_NGX_ProgressCallback_C InCallback)
    {
        NGXProgressCallbackHelper::tls_OriginalCallback = InCallback;
        NVSDK_NGX_Result Result = EvaluateFeatureFunc(InStream, InHandle, InParameters,
                                                      InCallback ? NGXProgressCallbackHelper::s_ReplacementCallback : nullptr);
        NGXProgressCallbackHelper::tls_OriginalCallback = nullptr;
        return Result;
    }

    template<class FuncType>
    static NVSDK_NGX_Result EvaluateFeature(FuncType EvaluateFeatureFunc,
                                            const NVSDK_NGX_Handle *InHandle, const NVSDK_NGX_Parameter *InParameters,
                                            PFN_NVSDK_NGX_ProgressCallback_C InCallback)
    {
        NGXProgressCallbackHelper::tls_OriginalCallback = InCallback;
        NVSDK_NGX_Result Result = EvaluateFeatureFunc(InHandle, InParameters,
                                                      InCallback ? NGXProgressCallbackHelper::s_ReplacementCallback : nullptr);
        NGXProgressCallbackHelper::tls_OriginalCallback = nullptr;
        return Result;
    }
};

NGXThreadLocal PFN_NVSDK_NGX_ProgressCallback_C NGXProgressCallbackHelper::tls_OriginalCallback = nullptr;

#endif /* NVSDK_NGX_HEADER_ONLY */
#endif /* NVSDK_NGX_STANDALONE_COMMON_H */
