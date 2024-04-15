// Copyright 2018 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#pragma once

#define OIDN_VERSION_MAJOR 2
#define OIDN_VERSION_MINOR 2
#define OIDN_VERSION_PATCH 2
#define OIDN_VERSION 20202
#define OIDN_VERSION_STRING "2.2.2"

/* #undef OIDN_API_NAMESPACE */
#define OIDN_STATIC_LIB

#if defined(OIDN_API_NAMESPACE)
  #define OIDN_API_NAMESPACE_BEGIN namespace  {
  #define OIDN_API_NAMESPACE_END }
  #define OIDN_API_NAMESPACE_USING using namespace ;
  #define OIDN_API_EXTERN_C
  #define OIDN_NAMESPACE ::oidn
  #define OIDN_NAMESPACE_C _oidn
  #define OIDN_NAMESPACE_BEGIN namespace  { namespace oidn {
  #define OIDN_NAMESPACE_END }}
#else
  #define OIDN_API_NAMESPACE_BEGIN
  #define OIDN_API_NAMESPACE_END
  #define OIDN_API_NAMESPACE_USING
  #if defined(__cplusplus)
    #define OIDN_API_EXTERN_C extern "C"
  #else
    #define OIDN_API_EXTERN_C
  #endif
  #define OIDN_NAMESPACE oidn
  #define OIDN_NAMESPACE_C oidn
  #define OIDN_NAMESPACE_BEGIN namespace oidn {
  #define OIDN_NAMESPACE_END }
#endif

#define OIDN_NAMESPACE_USING using namespace OIDN_NAMESPACE;

#if defined(OIDN_STATIC_LIB)
  #define OIDN_API_IMPORT OIDN_API_EXTERN_C
  #define OIDN_API_EXPORT OIDN_API_EXTERN_C
#elif defined(_WIN32)
  #define OIDN_API_IMPORT OIDN_API_EXTERN_C __declspec(dllimport)
  #define OIDN_API_EXPORT OIDN_API_EXTERN_C __declspec(dllexport)
#else
  #define OIDN_API_IMPORT OIDN_API_EXTERN_C
  #define OIDN_API_EXPORT OIDN_API_EXTERN_C __attribute__((visibility ("default")))
#endif

#if defined(OpenImageDenoise_EXPORTS)
  #define OIDN_API OIDN_API_EXPORT
#else
  #define OIDN_API OIDN_API_IMPORT
#endif

#if defined(_WIN32)
  #define OIDN_DEPRECATED(msg) __declspec(deprecated(msg))
#else
  #define OIDN_DEPRECATED(msg) __attribute__((deprecated(msg)))
#endif

#if !defined(OIDN_DEVICE_CPU)
/* #undef OIDN_DEVICE_CPU */
#endif
#if !defined(OIDN_DEVICE_SYCL)
/* #undef OIDN_DEVICE_SYCL */
#endif
#if !defined(OIDN_DEVICE_CUDA)
/* #undef OIDN_DEVICE_CUDA */
#endif
#if !defined(OIDN_DEVICE_HIP)
/* #undef OIDN_DEVICE_HIP */
#endif
#if !defined(OIDN_DEVICE_METAL)
/* #undef OIDN_DEVICE_METAL */
#endif

#define OIDN_FILTER_RT
#define OIDN_FILTER_RTLIGHTMAP
