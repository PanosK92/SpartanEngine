// Copyright 2018 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "oidn.h"
#include <cstdint>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <type_traits>
#include <vector>
#include <array>
#include <string>

OIDN_NAMESPACE_BEGIN

  // -----------------------------------------------------------------------------------------------
  // Flags helper type
  // -----------------------------------------------------------------------------------------------

  template<typename FlagT>
  struct IsFlag
  {
    static constexpr bool value = false;
  };

  template<typename FlagT>
  class Flags
  {
  public:
    static_assert(IsFlag<FlagT>::value, "not a flag type");

    using MaskType = typename std::underlying_type<FlagT>::type;

    constexpr Flags() noexcept : mask(0) {}
    constexpr Flags(FlagT flag) noexcept : mask(static_cast<MaskType>(flag)) {}
    constexpr Flags(const Flags& b) noexcept = default;
    constexpr explicit Flags(MaskType mask) noexcept : mask(mask) {}

    constexpr bool operator !() const noexcept { return !mask; }

    constexpr Flags operator &(const Flags& b) const noexcept { return Flags(mask & b.mask); }
    constexpr Flags operator |(const Flags& b) const noexcept { return Flags(mask | b.mask); }
    constexpr Flags operator ^(const Flags& b) const noexcept { return Flags(mask ^ b.mask); }

    Flags& operator =(const Flags& b) noexcept = default;

    Flags& operator &=(const Flags& b) noexcept
    {
      mask &= b.mask;
      return *this;
    }

    Flags& operator |=(const Flags& b) noexcept
    {
      mask |= b.mask;
      return *this;
    }

    Flags& operator ^=(const Flags& b) noexcept
    {
      mask ^= b.mask;
      return *this;
    }

    constexpr bool operator ==(const Flags& b) const noexcept { return mask == b.mask; }
    constexpr bool operator !=(const Flags& b) const noexcept { return mask != b.mask; }

    constexpr explicit operator bool() const noexcept { return mask; }
    constexpr explicit operator MaskType() const noexcept { return mask; }

  private:
    MaskType mask;
  };

  template<typename FlagT>
  inline constexpr Flags<FlagT> operator &(FlagT a, const Flags<FlagT>& b) noexcept
  {
    return Flags<FlagT>(a) & b;
  }

  template<typename FlagT>
  inline constexpr Flags<FlagT> operator |(FlagT a, const Flags<FlagT>& b) noexcept
  {
    return Flags<FlagT>(a) | b;
  }

  template<typename FlagT>
  inline constexpr Flags<FlagT> operator ^(FlagT a, const Flags<FlagT>& b) noexcept
  {
    return Flags<FlagT>(a) ^ b;
  }

  template<typename FlagT, typename std::enable_if<IsFlag<FlagT>::value, bool>::type = true>
  inline constexpr Flags<FlagT> operator &(FlagT a, FlagT b) noexcept
  {
    return Flags<FlagT>(a) & b;
  }

  template<typename FlagT, typename std::enable_if<IsFlag<FlagT>::value, bool>::type = true>
  inline constexpr Flags<FlagT> operator |(FlagT a, FlagT b) noexcept
  {
    return Flags<FlagT>(a) | b;
  }

  template<typename FlagT, typename std::enable_if<IsFlag<FlagT>::value, bool>::type = true>
  inline constexpr Flags<FlagT> operator ^(FlagT a, FlagT b) noexcept
  {
    return Flags<FlagT>(a) ^ b;
  }

  // -----------------------------------------------------------------------------------------------
  // Buffer
  // -----------------------------------------------------------------------------------------------

  // Formats for images and other data stored in buffers
  enum class Format
  {
    Undefined = OIDN_FORMAT_UNDEFINED,

    // 32-bit single-precision floating-point scalar and vector formats
    Float  = OIDN_FORMAT_FLOAT,
    Float2 = OIDN_FORMAT_FLOAT2,
    Float3 = OIDN_FORMAT_FLOAT3,
    Float4 = OIDN_FORMAT_FLOAT4,

    // 16-bit half-precision floating-point scalar and vector formats
    Half  = OIDN_FORMAT_HALF,
    Half2 = OIDN_FORMAT_HALF2,
    Half3 = OIDN_FORMAT_HALF3,
    Half4 = OIDN_FORMAT_HALF4,
  };

  // Storage modes for buffers
  enum class Storage
  {
    Undefined = OIDN_STORAGE_UNDEFINED,

    // stored on the host, accessible by both host and device
    Host      = OIDN_STORAGE_HOST,

    // stored on the device, *not* accessible by the host
    Device    = OIDN_STORAGE_DEVICE,

    // automatically migrated between host and device, accessible by both
    // *not* supported by all devices, "managedMemorySupported" device parameter should be checked
    Managed   = OIDN_STORAGE_MANAGED,
  };

  // External memory type flags
  enum class ExternalMemoryTypeFlag
  {
    None = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_NONE,

    // opaque POSIX file descriptor handle
    OpaqueFD = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_FD,

    // file descriptor handle for a Linux dma_buf
    DMABuf = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF,

    // NT handle
    OpaqueWin32 = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,

    // global share (KMT) handle
    OpaqueWin32KMT = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32_KMT,

    // NT handle returned by IDXGIResource1::CreateSharedHandle referring to a Direct3D 11
    // texture resource
    D3D11Texture = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_TEXTURE,

    // global share (KMT) handle returned by IDXGIResource::GetSharedHandle referring to a
    // Direct3D 11 texture resource
    D3D11TextureKMT = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_TEXTURE_KMT,

    // NT handle returned by IDXGIResource1::CreateSharedHandle referring to a Direct3D 11
    // resource
    D3D11Resource = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_RESOURCE,

    // global share (KMT) handle returned by IDXGIResource::GetSharedHandle referring to a
    // Direct3D 11 resource
    D3D11ResourceKMT = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_RESOURCE_KMT,

    // NT handle returned by ID3D12Device::CreateSharedHandle referring to a Direct3D 12
    // heap resource
    D3D12Heap = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_HEAP,

    // NT handle returned by ID3D12Device::CreateSharedHandle referring to a Direct3D 12
    // committed resource
    D3D12Resource = OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE,
  };

  template<> struct IsFlag<ExternalMemoryTypeFlag> { static constexpr bool value = true; };
  using ExternalMemoryTypeFlags = Flags<ExternalMemoryTypeFlag>;

  // Buffer object with automatic reference counting
  class BufferRef
  {
  public:
    BufferRef() : handle(nullptr) {}
    BufferRef(OIDNBuffer handle) : handle(handle) {}

    BufferRef(const BufferRef& other) : handle(other.handle)
    {
      if (handle)
        oidnRetainBuffer(handle);
    }

    BufferRef(BufferRef&& other) noexcept : handle(other.handle)
    {
      other.handle = nullptr;
    }

    BufferRef& operator =(const BufferRef& other)
    {
      if (&other != this)
      {
        if (other.handle)
          oidnRetainBuffer(other.handle);
        if (handle)
          oidnReleaseBuffer(handle);
        handle = other.handle;
      }
      return *this;
    }

    BufferRef& operator =(BufferRef&& other) noexcept
    {
      std::swap(handle, other.handle);
      return *this;
    }

    BufferRef& operator =(OIDNBuffer other)
    {
      if (other)
        oidnRetainBuffer(other);
      if (handle)
        oidnReleaseBuffer(handle);
      handle = other;
      return *this;
    }

    ~BufferRef()
    {
      if (handle)
        oidnReleaseBuffer(handle);
    }

    OIDNBuffer getHandle() const
    {
      return handle;
    }

    operator bool() const
    {
      return handle != nullptr;
    }

    // Releases the buffer (decrements the reference count).
    void release()
    {
      if (handle)
      {
        oidnReleaseBuffer(handle);
        handle = nullptr;
      }
    }

    // Gets the size of the buffer in bytes.
    size_t getSize() const
    {
      return oidnGetBufferSize(handle);
    }

    // Gets the storage mode of the buffer.
    Storage getStorage() const
    {
      return static_cast<Storage>(oidnGetBufferStorage(handle));
    }

    // Gets a pointer to the buffer data, which is accessible to the device but not necessarily to
    // the host as well, depending on the storage mode. Null pointer may be returned if the buffer
    // is empty or getting a pointer to data with device storage is not supported by the device.
    void* getData() const
    {
      return oidnGetBufferData(handle);
    }

    // Copies data from a region of the buffer to host memory.
    void read(size_t byteOffset, size_t byteSize, void* dstHostPtr) const
    {
      oidnReadBuffer(handle, byteOffset, byteSize, dstHostPtr);
    }

    // Copies data from a region of the buffer to host memory asynchronously.
    void readAsync(size_t byteOffset, size_t byteSize, void* dstHostPtr) const
    {
      oidnReadBufferAsync(handle, byteOffset, byteSize, dstHostPtr);
    }

    // Copies data to a region of the buffer from host memory.
    void write(size_t byteOffset, size_t byteSize, const void* srcHostPtr)
    {
      oidnWriteBuffer(handle, byteOffset, byteSize, srcHostPtr);
    }

    // Copies data to a region of the buffer from host memory asynchronously.
    void writeAsync(size_t byteOffset, size_t byteSize, const void* srcHostPtr)
    {
      oidnWriteBufferAsync(handle, byteOffset, byteSize, srcHostPtr);
    }

  private:
    OIDNBuffer handle;
  };

  // -----------------------------------------------------------------------------------------------
  // Filter
  // -----------------------------------------------------------------------------------------------

  // Filter quality/performance modes
  enum class Quality
  {
    Default  = OIDN_QUALITY_DEFAULT,  // default quality

    Balanced = OIDN_QUALITY_BALANCED, // balanced quality/performance (for interactive/real-time rendering)
    High     = OIDN_QUALITY_HIGH,     // high quality (for final-frame rendering)
  };

  // Progress monitor callback function
  using ProgressMonitorFunction = OIDNProgressMonitorFunction;

  // Filter object with automatic reference counting
  class FilterRef
  {
  public:
    FilterRef() : handle(nullptr) {}
    FilterRef(OIDNFilter handle) : handle(handle) {}

    FilterRef(const FilterRef& other) : handle(other.handle)
    {
      if (handle)
        oidnRetainFilter(handle);
    }

    FilterRef(FilterRef&& other) noexcept : handle(other.handle)
    {
      other.handle = nullptr;
    }

    FilterRef& operator =(const FilterRef& other)
    {
      if (&other != this)
      {
        if (other.handle)
          oidnRetainFilter(other.handle);
        if (handle)
          oidnReleaseFilter(handle);
        handle = other.handle;
      }
      return *this;
    }

    FilterRef& operator =(FilterRef&& other) noexcept
    {
      std::swap(handle, other.handle);
      return *this;
    }

    FilterRef& operator =(OIDNFilter other)
    {
      if (other)
        oidnRetainFilter(other);
      if (handle)
        oidnReleaseFilter(handle);
      handle = other;
      return *this;
    }

    ~FilterRef()
    {
      if (handle)
        oidnReleaseFilter(handle);
    }

    OIDNFilter getHandle() const
    {
      return handle;
    }

    operator bool() const
    {
      return handle != nullptr;
    }

    // Releases the filter (decrements the reference count).
    void release()
    {
      if (handle)
      {
        oidnReleaseFilter(handle);
        handle = nullptr;
      }
    }

    // Sets an image parameter of the filter with data stored in a buffer.
    void setImage(const char* name,
                  const BufferRef& buffer, Format format,
                  size_t width, size_t height,
                  size_t byteOffset = 0,
                  size_t pixelByteStride = 0, size_t rowByteStride = 0)
    {
      oidnSetFilterImage(handle, name,
                         buffer.getHandle(), static_cast<OIDNFormat>(format),
                         width, height,
                         byteOffset,
                         pixelByteStride, rowByteStride);
    }

    // Sets an image parameter of the filter with data owned by the user and accessible to the device.
    void setImage(const char* name,
                  void* devPtr, Format format,
                  size_t width, size_t height,
                  size_t byteOffset = 0,
                  size_t pixelByteStride = 0, size_t rowByteStride = 0)
    {
      oidnSetSharedFilterImage(handle, name,
                               devPtr, static_cast<OIDNFormat>(format),
                               width, height,
                               byteOffset,
                               pixelByteStride, rowByteStride);
    }

    // Unsets an image parameter of the filter that was previously set.
    void unsetImage(const char* name)
    {
      oidnUnsetFilterImage(handle, name);
    }

    OIDN_DEPRECATED("removeImage is deprecated. Use unsetImage instead.")
    void removeImage(const char* name)
    {
      oidnUnsetFilterImage(handle, name);
    }

    // Sets an opaque data parameter of the filter owned by the user and accessible to the host.
    void setData(const char* name, void* hostPtr, size_t byteSize)
    {
      oidnSetSharedFilterData(handle, name, hostPtr, byteSize);
    }

    // Notifies the filter that the contents of an opaque data parameter has been changed.
    void updateData(const char* name)
    {
      oidnUpdateFilterData(handle, name);
    }

    // Unsets an opaque data parameter of the filter that was previously set.
    void unsetData(const char* name)
    {
      oidnUnsetFilterData(handle, name);
    }

    OIDN_DEPRECATED("removeData is deprecated. Use unsetData instead.")
    void removeData(const char* name)
    {
      oidnUnsetFilterData(handle, name);
    }

    // Sets a boolean parameter of the filter.
    void set(const char* name, bool value)
    {
      oidnSetFilterBool(handle, name, value);
    }

    // Sets an integer parameter of the filter.
    void set(const char* name, int value)
    {
      oidnSetFilterInt(handle, name, value);
    }

    void set(const char* name, Quality value)
    {
      oidnSetFilterInt(handle, name, static_cast<int>(value));
    }

    // Sets a float parameter of the filter.
    void set(const char* name, float value)
    {
      oidnSetFilterFloat(handle, name, value);
    }

    // Gets a parameter of the filter.
    template<typename T>
    T get(const char* name) const;

    // Sets the progress monitor callback function of the filter.
    void setProgressMonitorFunction(ProgressMonitorFunction func, void* userPtr = nullptr)
    {
      oidnSetFilterProgressMonitorFunction(handle, func, userPtr);
    }

    // Commits all previous changes to the filter.
    void commit()
    {
      oidnCommitFilter(handle);
    }

    // Executes the filter.
    void execute()
    {
      oidnExecuteFilter(handle);
    }

    // Executes the filter asynchronously.
    void executeAsync()
    {
      oidnExecuteFilterAsync(handle);
    }

  #if defined(SYCL_LANGUAGE_VERSION)
    // Executes the filter of a SYCL device using the specified dependent events asynchronously, and
    // optionally returns an event for completion.
    sycl::event executeAsync(const std::vector<sycl::event>& depEvents)
    {
      sycl::event doneEvent;
      oidnExecuteSYCLFilterAsync(handle, depEvents.data(), static_cast<int>(depEvents.size()), &doneEvent);
      return doneEvent;
    }
  #endif

  private:
    OIDNFilter handle;
  };

  template<>
  inline bool FilterRef::get(const char* name) const
  {
    return oidnGetFilterBool(handle, name);
  }

  template<>
  inline int FilterRef::get(const char* name) const
  {
    return oidnGetFilterInt(handle, name);
  }

  template<>
  inline Quality FilterRef::get(const char* name) const
  {
    return static_cast<Quality>(oidnGetFilterInt(handle, name));
  }

  template<>
  inline float FilterRef::get(const char* name) const
  {
    return oidnGetFilterFloat(handle, name);
  }

  // -----------------------------------------------------------------------------------------------
  // Device
  // -----------------------------------------------------------------------------------------------

  // Device types
  enum class DeviceType
  {
    Default = OIDN_DEVICE_TYPE_DEFAULT, // select device automatically

    CPU   = OIDN_DEVICE_TYPE_CPU,   // CPU device
    SYCL  = OIDN_DEVICE_TYPE_SYCL,  // SYCL device
    CUDA  = OIDN_DEVICE_TYPE_CUDA,  // CUDA device
    HIP   = OIDN_DEVICE_TYPE_HIP,   // HIP device
    Metal = OIDN_DEVICE_TYPE_METAL, // Metal device
  };

  // Error codes
  enum class Error
  {
    None                = OIDN_ERROR_NONE,                 // no error occurred
    Unknown             = OIDN_ERROR_UNKNOWN,              // an unknown error occurred
    InvalidArgument     = OIDN_ERROR_INVALID_ARGUMENT,     // an invalid argument was specified
    InvalidOperation    = OIDN_ERROR_INVALID_OPERATION,    // the operation is not allowed
    OutOfMemory         = OIDN_ERROR_OUT_OF_MEMORY,        // not enough memory to execute the operation
    UnsupportedHardware = OIDN_ERROR_UNSUPPORTED_HARDWARE, // the hardware (e.g. CPU) is not supported
    Cancelled           = OIDN_ERROR_CANCELLED,            // the operation was cancelled by the user
  };

  // Error callback function
  typedef void (*ErrorFunction)(void* userPtr, Error code, const char* message);

  // Opaque universally unique identifier (UUID) of a physical device
  struct UUID
  {
    uint8_t bytes[OIDN_UUID_SIZE];
  };

  // Opaque locally unique identifier (LUID) of a physical device
  struct LUID
  {
    union
    {
      struct
      {
        uint32_t low;
        int32_t  high;
      };
      uint8_t bytes[OIDN_LUID_SIZE];
    };
  };

  // Device object with automatic reference counting
  class DeviceRef
  {
  public:
    DeviceRef() : handle(nullptr) {}
    DeviceRef(OIDNDevice handle) : handle(handle) {}

    DeviceRef(const DeviceRef& other) : handle(other.handle)
    {
      if (handle)
        oidnRetainDevice(handle);
    }

    DeviceRef(DeviceRef&& other) noexcept : handle(other.handle)
    {
      other.handle = nullptr;
    }

    DeviceRef& operator =(const DeviceRef& other)
    {
      if (&other != this)
      {
        if (other.handle)
          oidnRetainDevice(other.handle);
        if (handle)
          oidnReleaseDevice(handle);
        handle = other.handle;
      }
      return *this;
    }

    DeviceRef& operator =(DeviceRef&& other) noexcept
    {
      std::swap(handle, other.handle);
      return *this;
    }

    DeviceRef& operator =(OIDNDevice other)
    {
      if (other)
        oidnRetainDevice(other);
      if (handle)
        oidnReleaseDevice(handle);
      handle = other;
      return *this;
    }

    ~DeviceRef()
    {
      if (handle)
        oidnReleaseDevice(handle);
    }

    OIDNDevice getHandle() const
    {
      return handle;
    }

    operator bool() const
    {
      return handle != nullptr;
    }

    // Releases the device (decrements the reference count).
    void release()
    {
      if (handle)
      {
        oidnReleaseDevice(handle);
        handle = nullptr;
      }
    }

    // Sets a boolean parameter of the device.
    void set(const char* name, bool value)
    {
      oidnSetDeviceBool(handle, name, value);
    }

    // Sets an integer parameter of the device.
    void set(const char* name, int value)
    {
      oidnSetDeviceInt(handle, name, value);
    }

    // Sets an unsigned integer parameter of the device.
    void set(const char* name, unsigned int value)
    {
      oidnSetDeviceUInt(handle, name, value);
    }

    // Gets a parameter of the device.
    template<typename T>
    T get(const char* name) const;

    // Sets the error callback function of the device.
    void setErrorFunction(ErrorFunction func, void* userPtr = nullptr)
    {
      oidnSetDeviceErrorFunction(handle, reinterpret_cast<OIDNErrorFunction>(func), userPtr);
    }

    // Returns the first unqueried error code and clears the stored error.
    // Can be called for a null device as well to check for global errors (e.g. why a device
    // creation or physical device query has failed.
    Error getError()
    {
      return static_cast<Error>(oidnGetDeviceError(handle, nullptr));
    }

    // Returns the first unqueried error code and string message, and clears the stored error.
    // Can be called for a null device as well to check why a device creation failed.
    Error getError(const char*& outMessage)
    {
      return static_cast<Error>(oidnGetDeviceError(handle, &outMessage));
    }

    // Commits all previous changes to the device.
    // Must be called before first using the device (e.g. creating filters).
    void commit()
    {
      oidnCommitDevice(handle);
    }

    // Waits for all asynchronous operations running on the device to complete.
    void sync()
    {
      oidnSyncDevice(handle);
    }

    // Creates a buffer accessible to both the host and device.
    BufferRef newBuffer(size_t byteSize) const
    {
      return oidnNewBuffer(handle, byteSize);
    }

    // Creates a buffer with the specified storage mode.
    BufferRef newBuffer(size_t byteSize, Storage storage) const
    {
      return oidnNewBufferWithStorage(handle, byteSize, static_cast<OIDNStorage>(storage));
    }

    // Creates a shared buffer from memory allocated and owned by the user and accessible to the
    // device.
    BufferRef newBuffer(void* ptr, size_t byteSize) const
    {
      return oidnNewSharedBuffer(handle, ptr, byteSize);
    }

    // Creates a shared buffer by importing external memory from a POSIX file descriptor.
    BufferRef newBuffer(ExternalMemoryTypeFlag fdType, int fd, size_t byteSize) const
    {
      return oidnNewSharedBufferFromFD(
        handle, static_cast<OIDNExternalMemoryTypeFlag>(fdType), fd, byteSize);
    }

    // Creates a shared buffer by importing external memory from a Win32 handle.
    BufferRef newBuffer(ExternalMemoryTypeFlag handleType, void* handle, const void* name, size_t byteSize) const
    {
      return oidnNewSharedBufferFromWin32Handle(
        this->handle, static_cast<OIDNExternalMemoryTypeFlag>(handleType), handle, name, byteSize);
    }

    // Creates a shared buffer from a Metal buffer.
    // Only buffers with shared or private storage and hazard tracking are supported.
  #if defined(__OBJC__)
    BufferRef newBuffer(id<MTLBuffer> buffer) const
    {
      return oidnNewSharedBufferFromMetal(handle, buffer);
    }
  #endif

    // Creates a filter of the specified type (e.g. "RT").
    FilterRef newFilter(const char* type) const
    {
      return oidnNewFilter(handle, type);
    }

  private:
    OIDNDevice handle;
  };

  template<>
  inline bool DeviceRef::get(const char* name) const
  {
    return oidnGetDeviceBool(handle, name);
  }

  template<>
  inline int DeviceRef::get(const char* name) const
  {
    return oidnGetDeviceInt(handle, name);
  }

  template<>
  inline unsigned int DeviceRef::get(const char* name) const
  {
    return oidnGetDeviceUInt(handle, name);
  }

  template<>
  inline DeviceType DeviceRef::get(const char* name) const
  {
    return static_cast<DeviceType>(oidnGetDeviceInt(handle, name));
  }

  template<>
  inline ExternalMemoryTypeFlags DeviceRef::get(const char* name) const
  {
    return ExternalMemoryTypeFlags(oidnGetDeviceInt(handle, name));
  }

  // Returns the first unqueried per-thread global error code and clears the stored error.
  inline Error getError()
  {
    return static_cast<Error>(oidnGetDeviceError(nullptr, nullptr));
  }

  // Returns the first unqueried per-thread global error code and string message, and clears the
  // stored error.
  inline Error getError(const char*& outMessage)
  {
    return static_cast<Error>(oidnGetDeviceError(nullptr, &outMessage));
  }

  // Creates a device of the specified type.
  inline DeviceRef newDevice(DeviceType type = DeviceType::Default)
  {
    return DeviceRef(oidnNewDevice(static_cast<OIDNDeviceType>(type)));
  }

  // Creates a device from a physical device specified by its ID (0 to getNumPhysicalDevices()-1).
  inline DeviceRef newDevice(int physicalDeviceID)
  {
    return DeviceRef(oidnNewDeviceByID(physicalDeviceID));
  }

  // Creates a device from a physical device specified by its UUID.
  inline DeviceRef newDevice(const UUID& uuid)
  {
    return DeviceRef(oidnNewDeviceByUUID(uuid.bytes));
  }

  // Creates a device from a physical device specified by its LUID.
  inline DeviceRef newDevice(const LUID& luid)
  {
    return DeviceRef(oidnNewDeviceByLUID(luid.bytes));
  }

  // Creates a device from a physical device specified by its PCI address.
  inline DeviceRef newDevice(int pciDomain, int pciBus, int pciDevice, int pciFunction)
  {
    return DeviceRef(oidnNewDeviceByPCIAddress(pciDomain, pciBus, pciDevice, pciFunction));
  }

#if defined(SYCL_LANGUAGE_VERSION)
  // Creates a device from the specified SYCL queue.
  inline DeviceRef newSYCLDevice(const sycl::queue& queue)
  {
    return DeviceRef(oidnNewSYCLDevice(&queue, 1));
  }

  // Creates a device from the specified list of SYCL queues.
  // The queues should belong to different SYCL sub-devices (Xe Stack/Tile) of the same SYCL
  // root-device (GPU).
  inline DeviceRef newSYCLDevice(const std::vector<sycl::queue>& queues)
  {
    return DeviceRef(oidnNewSYCLDevice(queues.data(), static_cast<int>(queues.size())));
  }
#endif

  // Creates a device from the specified CUDA device ID (negative value maps to the current device)
  // and stream.
  inline DeviceRef newCUDADevice(int deviceID, cudaStream_t stream)
  {
    return DeviceRef(oidnNewCUDADevice(&deviceID, &stream, 1));
  }

  // Creates a device from the specified pairs of CUDA device IDs (negative ID corresponds to the
  // current device) and streams (null stream corresponds to the default stream).
  // Currently only one device ID/stream is supported.
  inline DeviceRef newCUDADevice(const std::vector<int>& deviceIDs,
                                 const std::vector<cudaStream_t>& streams)
  {
    assert(deviceIDs.size() == streams.size());
    return DeviceRef(oidnNewCUDADevice(deviceIDs.data(), streams.data(),
                                       static_cast<int>(streams.size())));
  }

  // Creates a device from the specified HIP device ID (negative ID corresponds to the current
  // device) and stream (null stream corresponds to the default stream).
  inline DeviceRef newHIPDevice(int deviceID, hipStream_t stream)
  {
    return DeviceRef(oidnNewHIPDevice(&deviceID, &stream, 1));
  }

  // Creates a device from the specified pairs of HIP device IDs (negative ID corresponds to the
  // current device) and streams (null stream corresponds to the default stream).
  // Currently only one device ID/stream is supported.
  inline DeviceRef newHIPDevice(const std::vector<int>& deviceIDs,
                                const std::vector<hipStream_t>& streams)
  {
    assert(deviceIDs.size() == streams.size());
    return DeviceRef(oidnNewHIPDevice(deviceIDs.data(), streams.data(),
                                      static_cast<int>(streams.size())));
  }

  // Creates a device from the specified Metal command queue.
  inline DeviceRef newMetalDevice(MTLCommandQueue_id commandQueue)
  {
    return DeviceRef(oidnNewMetalDevice(&commandQueue, 1));
  }

  // Creates a device from the specified list of Metal command queues.
  // Currently only one queue is supported.
  inline DeviceRef newMetalDevice(const std::vector<MTLCommandQueue_id>& commandQueues)
  {
    return DeviceRef(oidnNewMetalDevice(commandQueues.data(), static_cast<int>(commandQueues.size())));
  }

  // -----------------------------------------------------------------------------------------------
  // Physical Device
  // -----------------------------------------------------------------------------------------------

  class PhysicalDeviceRef
  {
  public:
    PhysicalDeviceRef() : id(-1) {}
    PhysicalDeviceRef(int id) : id(id) {}

    PhysicalDeviceRef& operator =(int other)
    {
      id = other;
      return *this;
    }

    int getID() const
    {
      return id;
    }

    operator bool() const
    {
      return id >= 0;
    }

    // Gets a paramter of the physical device.
    template<typename T>
    T get(const char* name) const;

    // Gets an opaque data parameter of the physical device.
    std::pair<const void*, size_t> getData(const char* name) const
    {
      size_t byteSize = 0;
      const void* ptr = oidnGetPhysicalDeviceData(id, name, &byteSize);
      return {ptr, byteSize};
    }

    // Creates a device from the physical device.
    DeviceRef newDevice()
    {
      return DeviceRef(oidnNewDeviceByID(id));
    }

  private:
    int id;
  };

  // Returns the number of supported physical devices.
  inline int getNumPhysicalDevices()
  {
    return oidnGetNumPhysicalDevices();
  }

  template<>
  inline bool PhysicalDeviceRef::get(const char* name) const
  {
    return oidnGetPhysicalDeviceBool(id, name);
  }

  template<>
  inline int PhysicalDeviceRef::get(const char* name) const
  {
    return oidnGetPhysicalDeviceInt(id, name);
  }

  template<>
  inline unsigned int PhysicalDeviceRef::get(const char* name) const
  {
    return oidnGetPhysicalDeviceUInt(id, name);
  }

  template<>
  inline DeviceType PhysicalDeviceRef::get(const char* name) const
  {
    return static_cast<DeviceType>(oidnGetPhysicalDeviceInt(id, name));
  }

  template<>
  inline const char* PhysicalDeviceRef::get(const char* name) const
  {
    return oidnGetPhysicalDeviceString(id, name);
  }

  template<>
  inline std::string PhysicalDeviceRef::get(const char* name) const
  {
    const char* str = oidnGetPhysicalDeviceString(id, name);
    return str ? str : "";
  }

  template<>
  inline UUID PhysicalDeviceRef::get(const char* name) const
  {
    UUID uuid{};
    auto data = getData(name);
    if (data.first != nullptr)
    {
      if (data.second == sizeof(uuid.bytes))
        std::memcpy(uuid.bytes, data.first, sizeof(uuid.bytes));
      else
        getData(""); // invoke an error
    }
    return uuid;
  }

  template<>
  inline LUID PhysicalDeviceRef::get(const char* name) const
  {
    LUID luid{};
    auto data = getData(name);
    if (data.first != nullptr)
    {
      if (data.second == sizeof(luid.bytes))
        std::memcpy(luid.bytes, data.first, sizeof(luid.bytes));
      else
        getData(""); // invoke an error
    }
    return luid;
  }

OIDN_NAMESPACE_END
