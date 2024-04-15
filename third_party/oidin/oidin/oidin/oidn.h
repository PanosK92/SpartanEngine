// Copyright 2018 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#if defined(__cplusplus)
  #if defined(SYCL_LANGUAGE_VERSION)
    #include <CL/sycl.hpp>
  #else
    namespace sycl
    {
      class queue;
      class event;
    }
  #endif
#endif

typedef struct CUstream_st* cudaStream_t;
typedef struct ihipStream_t* hipStream_t;

#if defined(__OBJC__)
  @protocol MTLCommandQueue;
  @protocol MTLBuffer;

  typedef id<MTLCommandQueue> MTLCommandQueue_id;
  typedef id<MTLBuffer> MTLBuffer_id;
#else
  typedef void* MTLCommandQueue_id;
  typedef void* MTLBuffer_id;
#endif

OIDN_API_NAMESPACE_BEGIN

// -------------------------------------------------------------------------------------------------
// Physical Device
// -------------------------------------------------------------------------------------------------

#define OIDN_UUID_SIZE 16u // size of a universally unique identifier (UUID) of a physical device
#define OIDN_LUID_SIZE 8u  // size of a locally unique identifier (LUID) of a physical device

// Returns the number of supported physical devices.
OIDN_API int oidnGetNumPhysicalDevices();

// Gets a boolean parameter of the physical device.
OIDN_API bool oidnGetPhysicalDeviceBool(int physicalDeviceID, const char* name);

// Gets an integer parameter of the physical device.
OIDN_API int oidnGetPhysicalDeviceInt(int physicalDeviceID, const char* name);

// Gets an unsigned integer parameter of the physical device.
inline unsigned int oidnGetPhysicalDeviceUInt(int physicalDeviceID, const char* name)
{
  return (unsigned int)oidnGetPhysicalDeviceInt(physicalDeviceID, name);
}

// Gets a string parameter of the physical device.
OIDN_API const char* oidnGetPhysicalDeviceString(int physicalDeviceID, const char* name);

// Gets an opaque data parameter of the physical device.
OIDN_API const void* oidnGetPhysicalDeviceData(int physicalDeviceID, const char* name,
                                               size_t* byteSize);

// -------------------------------------------------------------------------------------------------
// Device
// -------------------------------------------------------------------------------------------------

// Device types
typedef enum
{
  OIDN_DEVICE_TYPE_DEFAULT = 0, // select device automatically

  OIDN_DEVICE_TYPE_CPU   = 1, // CPU device
  OIDN_DEVICE_TYPE_SYCL  = 2, // SYCL device
  OIDN_DEVICE_TYPE_CUDA  = 3, // CUDA device
  OIDN_DEVICE_TYPE_HIP   = 4, // HIP device
  OIDN_DEVICE_TYPE_METAL = 5, // Metal device
} OIDNDeviceType;

// Error codes
typedef enum
{
  OIDN_ERROR_NONE                 = 0, // no error occurred
  OIDN_ERROR_UNKNOWN              = 1, // an unknown error occurred
  OIDN_ERROR_INVALID_ARGUMENT     = 2, // an invalid argument was specified
  OIDN_ERROR_INVALID_OPERATION    = 3, // the operation is not allowed
  OIDN_ERROR_OUT_OF_MEMORY        = 4, // not enough memory to execute the operation
  OIDN_ERROR_UNSUPPORTED_HARDWARE = 5, // the hardware (e.g. CPU) is not supported
  OIDN_ERROR_CANCELLED            = 6, // the operation was cancelled by the user
} OIDNError;

// Error callback function
typedef void (*OIDNErrorFunction)(void* userPtr, OIDNError code, const char* message);

// Device handle
typedef struct OIDNDeviceImpl* OIDNDevice;

// Creates a device of the specified type.
OIDN_API OIDNDevice oidnNewDevice(OIDNDeviceType type);

// Creates a device from a physical device specified by its ID (0 to oidnGetNumPhysicalDevices()-1).
OIDN_API OIDNDevice oidnNewDeviceByID(int physicalDeviceID);

// Creates a device from a physical device specified by its UUID.
OIDN_API OIDNDevice oidnNewDeviceByUUID(const void* uuid);

// Creates a device from a physical device specified by its LUID.
OIDN_API OIDNDevice oidnNewDeviceByLUID(const void* luid);

// Creates a device from a physical device specified by its PCI address.
OIDN_API OIDNDevice oidnNewDeviceByPCIAddress(int pciDomain, int pciBus, int pciDevice,
                                              int pciFunction);

#if defined(__cplusplus)
// Creates a device from the specified list of SYCL queues.
// The queues should belong to different SYCL sub-devices (Xe Stack/Tile) of the same SYCL
// root-device (GPU).
OIDN_API OIDNDevice oidnNewSYCLDevice(const sycl::queue* queues, int numQueues);
#endif

// Creates a device from the specified pairs of CUDA device IDs (negative ID corresponds to the
// current device) and streams (null stream corresponds to the default stream).
// Currently only one device ID/stream is supported.
OIDN_API OIDNDevice oidnNewCUDADevice(const int* deviceIDs, const cudaStream_t* streams,
                                      int numPairs);

// Creates a device from the specified pairs of HIP device IDs (negative ID corresponds to the
// current device) and streams (null stream corresponds to the default stream).
// Currently only one device ID/stream is supported.
OIDN_API OIDNDevice oidnNewHIPDevice(const int* deviceIDs, const hipStream_t* streams,
                                     int numPairs);

// Creates a device from the specified list of Metal command queues.
// Currently only one queue is supported.
OIDN_API OIDNDevice oidnNewMetalDevice(const MTLCommandQueue_id* commandQueues, int numQueues);

// Retains the device (increments the reference count).
OIDN_API void oidnRetainDevice(OIDNDevice device);

// Releases the device (decrements the reference count).
OIDN_API void oidnReleaseDevice(OIDNDevice device);

// Sets a boolean parameter of the device.
OIDN_API void oidnSetDeviceBool(OIDNDevice device, const char* name, bool value);

OIDN_DEPRECATED("oidnSetDevice1b is deprecated. Use oidnSetDeviceBool instead.")
inline void oidnSetDevice1b(OIDNDevice device, const char* name, bool value)
{
  oidnSetDeviceBool(device, name, value);
}

// Sets an integer parameter of the device.
OIDN_API void oidnSetDeviceInt(OIDNDevice device, const char* name, int value);

OIDN_DEPRECATED("oidnSetDevice1i is deprecated. Use oidnSetDeviceInt instead.")
inline void oidnSetDevice1i(OIDNDevice device, const char* name, int value)
{
  oidnSetDeviceInt(device, name, value);
}

// Sets an unsigned integer parameter of the device.
inline void oidnSetDeviceUInt(OIDNDevice device, const char* name, unsigned int value)
{
  oidnSetDeviceInt(device, name, (int)value);
}

// Gets a boolean parameter of the device.
OIDN_API bool oidnGetDeviceBool(OIDNDevice device, const char* name);

OIDN_DEPRECATED("oidnGetDevice1b is deprecated. Use oidnGetDeviceBool instead.")
inline bool oidnGetDevice1b(OIDNDevice device, const char* name)
{
  return oidnGetDeviceBool(device, name);
}

// Gets an integer parameter of the device.
OIDN_API int oidnGetDeviceInt(OIDNDevice device, const char* name);

// Gets an unsigned integer parameter of the device.
inline unsigned int oidnGetDeviceUInt(OIDNDevice device, const char* name)
{
  return (unsigned int)oidnGetDeviceInt(device, name);
}

OIDN_DEPRECATED("oidnGetDevice1i is deprecated. Use oidnGetDeviceInt instead.")
inline int oidnGetDevice1i(OIDNDevice device, const char* name)
{
  return oidnGetDeviceInt(device, name);
}

// Sets the error callback function of the device.
OIDN_API void oidnSetDeviceErrorFunction(OIDNDevice device, OIDNErrorFunction func, void* userPtr);

// Returns the first unqueried error code stored in the device for the current thread, optionally
// also returning a string message (if not NULL), and clears the stored error. Can be called with
// a NULL device as well to check for per-thread global errors (e.g. why a device creation or
// physical device query has failed).
OIDN_API OIDNError oidnGetDeviceError(OIDNDevice device, const char** outMessage);

// Commits all previous changes to the device.
// Must be called before first using the device (e.g. creating filters).
OIDN_API void oidnCommitDevice(OIDNDevice device);

// Waits for all asynchronous operations running on the device to complete.
OIDN_API void oidnSyncDevice(OIDNDevice device);

// -------------------------------------------------------------------------------------------------
// Buffer
// -------------------------------------------------------------------------------------------------

// Formats for images and other data stored in buffers
typedef enum
{
  OIDN_FORMAT_UNDEFINED = 0,

  // 32-bit single-precision floating-point scalar and vector formats
  OIDN_FORMAT_FLOAT  = 1,
  OIDN_FORMAT_FLOAT2,
  OIDN_FORMAT_FLOAT3,
  OIDN_FORMAT_FLOAT4,

  // 16-bit half-precision floating-point scalar and vector formats
  OIDN_FORMAT_HALF  = 257,
  OIDN_FORMAT_HALF2,
  OIDN_FORMAT_HALF3,
  OIDN_FORMAT_HALF4,
} OIDNFormat;

// Storage modes for buffers
typedef enum
{
  OIDN_STORAGE_UNDEFINED = 0,

  // stored on the host, accessible by both host and device
  OIDN_STORAGE_HOST      = 1,

  // stored on the device, *not* accessible by the host
  OIDN_STORAGE_DEVICE    = 2,

  // automatically migrated between host and device, accessible by both
  // *not* supported by all devices, "managedMemorySupported" device parameter should be checked
  OIDN_STORAGE_MANAGED   = 3,
} OIDNStorage;

// External memory type flags
typedef enum
{
  OIDN_EXTERNAL_MEMORY_TYPE_FLAG_NONE = 0,

  // opaque POSIX file descriptor handle
  OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_FD = 1 << 0,

  // file descriptor handle for a Linux dma_buf
  OIDN_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF = 1 << 1,

  // NT handle
  OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32 = 1 << 2,

  // global share (KMT) handle
  OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32_KMT = 1 << 3,

  // NT handle returned by IDXGIResource1::CreateSharedHandle referring to a Direct3D 11 texture
  // resource
  OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_TEXTURE = 1 << 4,

  // global share (KMT) handle returned by IDXGIResource::GetSharedHandle referring to a Direct3D 11
  // texture resource
  OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_TEXTURE_KMT = 1 << 5,

  // NT handle returned by IDXGIResource1::CreateSharedHandle referring to a Direct3D 11 resource
  OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_RESOURCE = 1 << 6,

  // global share (KMT) handle returned by IDXGIResource::GetSharedHandle referring to a Direct3D 11
  // resource
  OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_RESOURCE_KMT = 1 << 7,

  // NT handle returned by ID3D12Device::CreateSharedHandle referring to a Direct3D 12 heap
  // resource
  OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_HEAP = 1 << 8,

  // NT handle returned by ID3D12Device::CreateSharedHandle referring to a Direct3D 12 committed
  // resource
  OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE = 1 << 9,
} OIDNExternalMemoryTypeFlag;

// Buffer handle
typedef struct OIDNBufferImpl* OIDNBuffer;

// Creates a buffer accessible to both the host and device.
OIDN_API OIDNBuffer oidnNewBuffer(OIDNDevice device, size_t byteSize);

// Creates a buffer with the specified storage mode.
OIDN_API OIDNBuffer oidnNewBufferWithStorage(OIDNDevice device, size_t byteSize, OIDNStorage storage);

// Creates a shared buffer from memory allocated and owned by the user and accessible to the device.
OIDN_API OIDNBuffer oidnNewSharedBuffer(OIDNDevice device, void* devPtr, size_t byteSize);

// Creates a shared buffer by importing external memory from a POSIX file descriptor.
OIDN_API OIDNBuffer oidnNewSharedBufferFromFD(OIDNDevice device,
                                              OIDNExternalMemoryTypeFlag fdType,
                                              int fd, size_t byteSize);

// Creates a shared buffer by importing external memory from a Win32 handle.
OIDN_API OIDNBuffer oidnNewSharedBufferFromWin32Handle(OIDNDevice device,
                                                       OIDNExternalMemoryTypeFlag handleType,
                                                       void* handle, const void* name, size_t byteSize);

// Creates a shared buffer from a Metal buffer.
// Only buffers with shared or private storage and hazard tracking are supported.
OIDN_API OIDNBuffer oidnNewSharedBufferFromMetal(OIDNDevice device, MTLBuffer_id buffer);

// Gets the size of the buffer in bytes.
OIDN_API size_t oidnGetBufferSize(OIDNBuffer buffer);

// Gets the storage mode of the buffer.
OIDN_API OIDNStorage oidnGetBufferStorage(OIDNBuffer buffer);

// Gets a pointer to the buffer data, which is accessible to the device but not necessarily to
// the host as well, depending on the storage mode. Null pointer may be returned if the buffer
// is empty or getting a pointer to data with device storage is not supported by the device.
OIDN_API void* oidnGetBufferData(OIDNBuffer buffer);

// Copies data from a region of the buffer to host memory.
OIDN_API void oidnReadBuffer(OIDNBuffer buffer, size_t byteOffset, size_t byteSize, void* dstHostPtr);

// Copies data from a region of the buffer to host memory asynchronously.
OIDN_API void oidnReadBufferAsync(OIDNBuffer buffer,
                                  size_t byteOffset, size_t byteSize, void* dstHostPtr);

// Copies data to a region of the buffer from host memory.
OIDN_API void oidnWriteBuffer(OIDNBuffer buffer,
                              size_t byteOffset, size_t byteSize, const void* srcHostPtr);

// Copies data to a region of the buffer from host memory asynchronously.
OIDN_API void oidnWriteBufferAsync(OIDNBuffer buffer,
                                   size_t byteOffset, size_t byteSize, const void* srcHostPtr);

// Retains the buffer (increments the reference count).
OIDN_API void oidnRetainBuffer(OIDNBuffer buffer);

// Releases the buffer (decrements the reference count).
OIDN_API void oidnReleaseBuffer(OIDNBuffer buffer);

// -------------------------------------------------------------------------------------------------
// Filter
// -------------------------------------------------------------------------------------------------

// Filter quality/performance modes
typedef enum
{
  OIDN_QUALITY_DEFAULT  = 0, // default quality

//OIDN_QUALITY_FAST     = 4
  OIDN_QUALITY_BALANCED = 5, // balanced quality/performance (for interactive/real-time rendering)
  OIDN_QUALITY_HIGH     = 6, // high quality (for final-frame rendering)
} OIDNQuality;

// Progress monitor callback function
typedef bool (*OIDNProgressMonitorFunction)(void* userPtr, double n);

// Filter handle
typedef struct OIDNFilterImpl* OIDNFilter;

// Creates a filter of the specified type (e.g. "RT").
OIDN_API OIDNFilter oidnNewFilter(OIDNDevice device, const char* type);

// Retains the filter (increments the reference count).
OIDN_API void oidnRetainFilter(OIDNFilter filter);

// Releases the filter (decrements the reference count).
OIDN_API void oidnReleaseFilter(OIDNFilter filter);

// Sets an image parameter of the filter with data stored in a buffer.
// If pixelByteStride and/or rowByteStride are zero, these will be computed automatically.
OIDN_API void oidnSetFilterImage(OIDNFilter filter, const char* name,
                                 OIDNBuffer buffer, OIDNFormat format,
                                 size_t width, size_t height,
                                 size_t byteOffset,
                                 size_t pixelByteStride, size_t rowByteStride);

// Sets an image parameter of the filter with data owned by the user and accessible to the device.
// If pixelByteStride and/or rowByteStride are zero, these will be computed automatically.
OIDN_API void oidnSetSharedFilterImage(OIDNFilter filter, const char* name,
                                       void* devPtr, OIDNFormat format,
                                       size_t width, size_t height,
                                       size_t byteOffset,
                                       size_t pixelByteStride, size_t rowByteStride);

// Unsets an image parameter of the filter that was previously set.
OIDN_API void oidnUnsetFilterImage(OIDNFilter filter, const char* name);

OIDN_DEPRECATED("oidnRemoveFilterImage is deprecated. Use oidnUnsetFilterImage instead.")
inline void oidnRemoveFilterImage(OIDNFilter filter, const char* name)
{
  oidnUnsetFilterImage(filter, name);
}

// Sets an opaque data parameter of the filter owned by the user and accessible to the host.
OIDN_API void oidnSetSharedFilterData(OIDNFilter filter, const char* name,
                                      void* hostPtr, size_t byteSize);

// Notifies the filter that the contents of an opaque data parameter has been changed.
OIDN_API void oidnUpdateFilterData(OIDNFilter filter, const char* name);

// Unsets an opaque data parameter of the filter that was previously set.
OIDN_API void oidnUnsetFilterData(OIDNFilter filter, const char* name);

OIDN_DEPRECATED("oidnRemoveFilterData is deprecated. Use oidnUnsetFilterData instead.")
inline void oidnRemoveFilterData(OIDNFilter filter, const char* name)
{
  oidnUnsetFilterData(filter, name);
}

// Sets a boolean parameter of the filter.
OIDN_API void oidnSetFilterBool(OIDNFilter filter, const char* name, bool value);

OIDN_DEPRECATED("oidnSetFilter1b is deprecated. Use oidnSetFilterBool instead.")
inline void oidnSetFilter1b(OIDNFilter filter, const char* name, bool value)
{
  oidnSetFilterBool(filter, name, value);
}

// Gets a boolean parameter of the filter.
OIDN_API bool oidnGetFilterBool(OIDNFilter filter, const char* name);

OIDN_DEPRECATED("oidnGetFilter1b is deprecated. Use oidnGetFilterBool instead.")
inline bool oidnGetFilter1b(OIDNFilter filter, const char* name)
{
  return oidnGetFilterBool(filter, name);
}

// Sets an integer parameter of the filter.
OIDN_API void oidnSetFilterInt(OIDNFilter filter, const char* name, int value);

OIDN_DEPRECATED("oidnSetFilter1i is deprecated. Use oidnSetFilterInt instead.")
inline void oidnSetFilter1i(OIDNFilter filter, const char* name, int value)
{
  oidnSetFilterInt(filter, name, value);
}

// Gets an integer parameter of the filter.
OIDN_API int oidnGetFilterInt(OIDNFilter filter, const char* name);

OIDN_DEPRECATED("oidnGetFilter1i is deprecated. Use oidnGetFilterInt instead.")
inline int oidnGetFilter1i(OIDNFilter filter, const char* name)
{
  return oidnGetFilterInt(filter, name);
}

// Sets a float parameter of the filter.
OIDN_API void oidnSetFilterFloat(OIDNFilter filter, const char* name, float value);

OIDN_DEPRECATED("oidnSetFilter1f is deprecated. Use oidnSetFilterFloat instead.")
inline void oidnSetFilter1f(OIDNFilter filter, const char* name, float value)
{
  oidnSetFilterFloat(filter, name, value);
}

// Gets a float parameter of the filter.
OIDN_API float oidnGetFilterFloat(OIDNFilter filter, const char* name);

OIDN_DEPRECATED("oidnGetFilter1f is deprecated. Use oidnGetFilterFloat instead.")
inline float oidnGetFilter1f(OIDNFilter filter, const char* name)
{
  return oidnGetFilterFloat(filter, name);
}

// Sets the progress monitor callback function of the filter.
OIDN_API void oidnSetFilterProgressMonitorFunction(OIDNFilter filter,
                                                   OIDNProgressMonitorFunction func, void* userPtr);

// Commits all previous changes to the filter.
// Must be called before first executing the filter.
OIDN_API void oidnCommitFilter(OIDNFilter filter);

// Executes the filter.
OIDN_API void oidnExecuteFilter(OIDNFilter filter);

// Executes the filter asynchronously.
OIDN_API void oidnExecuteFilterAsync(OIDNFilter filter);

#if defined(__cplusplus)
// Executes the filter of a SYCL device using the specified dependent events asynchronously, and
// optionally returns an event for completion.
OIDN_API void oidnExecuteSYCLFilterAsync(OIDNFilter filter,
                                         const sycl::event* depEvents, int numDepEvents,
                                         sycl::event* doneEvent);
#endif

OIDN_API_NAMESPACE_END
