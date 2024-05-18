/*
Copyright(c) 2016-2024 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

// RUNTIME
#if defined(SPARTAN_RUNTIME) || (SPARTAN_RUNTIME_STATIC == 1)

// definition - DirectX 12
#if defined(API_GRAPHICS_D3D12)
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma warning(push, 0) // hide warnings which belong DirectX
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#pragma warning(pop)

static const D3D12_CULL_MODE d3d12_cull_mode[] =
{
    D3D12_CULL_MODE_NONE,
    D3D12_CULL_MODE_FRONT,
    D3D12_CULL_MODE_BACK
};

static const D3D12_FILL_MODE d3d12_polygon_mode[] =
{
    D3D12_FILL_MODE_SOLID,
    D3D12_FILL_MODE_WIREFRAME
};

static const D3D12_PRIMITIVE_TOPOLOGY_TYPE d3d12_primitive_topology[] =
{
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
};

static const DXGI_FORMAT d3d12_format[] =
{
    // R
    DXGI_FORMAT_R8_UNORM,
    DXGI_FORMAT_R8_UINT,
    DXGI_FORMAT_R16_UNORM,
    DXGI_FORMAT_R16_UINT,
    DXGI_FORMAT_R16_FLOAT,
    DXGI_FORMAT_R32_UINT,
    DXGI_FORMAT_R32_FLOAT,
    // RG
    DXGI_FORMAT_R8G8_UNORM,
    DXGI_FORMAT_R16G16_FLOAT,
    DXGI_FORMAT_R32G32_FLOAT,
    // RGB
    DXGI_FORMAT_R11G11B10_FLOAT,
    DXGI_FORMAT_R32G32B32_FLOAT,
    // RGBA
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R10G10B10A2_UNORM,
    DXGI_FORMAT_R16G16B16A16_UNORM,
    DXGI_FORMAT_R16G16B16A16_SNORM,
    DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT,
    // Depth
    DXGI_FORMAT_D16_UNORM,
    DXGI_FORMAT_D32_FLOAT,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
    // Compressed
    DXGI_FORMAT_BC1_UNORM,
    DXGI_FORMAT_BC3_UNORM,
    DXGI_FORMAT_BC5_UNORM,
    DXGI_FORMAT_BC7_UNORM,
    DXGI_FORMAT_UNKNOWN,
    // Surface
    DXGI_FORMAT_B8G8R8A8_UNORM,
    // Unknown
    DXGI_FORMAT_UNKNOWN
};

static const D3D12_TEXTURE_ADDRESS_MODE d3d12_sampler_address_mode[] =
{
    D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
    D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
    D3D12_TEXTURE_ADDRESS_MODE_BORDER,
    D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE
};

static const D3D12_COMPARISON_FUNC d3d12_comparison_function[] =
{
    D3D12_COMPARISON_FUNC_NEVER,
    D3D12_COMPARISON_FUNC_LESS,
    D3D12_COMPARISON_FUNC_EQUAL,
    D3D12_COMPARISON_FUNC_LESS_EQUAL,
    D3D12_COMPARISON_FUNC_GREATER,
    D3D12_COMPARISON_FUNC_NOT_EQUAL,
    D3D12_COMPARISON_FUNC_GREATER_EQUAL,
    D3D12_COMPARISON_FUNC_ALWAYS
};

static const D3D12_STENCIL_OP d3d12_stencil_operation[] =
{
    D3D12_STENCIL_OP_KEEP,
    D3D12_STENCIL_OP_ZERO,
    D3D12_STENCIL_OP_REPLACE,
    D3D12_STENCIL_OP_INCR_SAT,
    D3D12_STENCIL_OP_DECR_SAT,
    D3D12_STENCIL_OP_INVERT,
    D3D12_STENCIL_OP_INCR,
    D3D12_STENCIL_OP_DECR
};

static const D3D12_BLEND d3d12_blend_factor[] =
{
    D3D12_BLEND_ZERO,
    D3D12_BLEND_ONE,
    D3D12_BLEND_SRC_COLOR,
    D3D12_BLEND_INV_SRC_COLOR,
    D3D12_BLEND_SRC_ALPHA,
    D3D12_BLEND_INV_SRC_ALPHA,
    D3D12_BLEND_DEST_ALPHA,
    D3D12_BLEND_INV_DEST_ALPHA,
    D3D12_BLEND_DEST_COLOR,
    D3D12_BLEND_INV_DEST_COLOR,
    D3D12_BLEND_SRC_ALPHA_SAT,
    D3D12_BLEND_BLEND_FACTOR,
    D3D12_BLEND_INV_BLEND_FACTOR,
    D3D12_BLEND_SRC1_COLOR,
    D3D12_BLEND_INV_SRC1_COLOR,
    D3D12_BLEND_SRC1_ALPHA,
    D3D12_BLEND_INV_SRC1_ALPHA
};

static const D3D12_BLEND_OP d3d12_blend_operation[] =
{
    D3D12_BLEND_OP_ADD,
    D3D12_BLEND_OP_SUBTRACT,
    D3D12_BLEND_OP_REV_SUBTRACT,
    D3D12_BLEND_OP_MIN,
    D3D12_BLEND_OP_MAX
};

#endif

// definition - vulkan
#if defined(API_GRAPHICS_VULKAN) 
#pragma comment(lib, "vulkan-1.lib")
#if defined(_MSC_VER)
// include definition of vkCreateWin32SurfaceKHR via vulkan.h
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#pragma warning(push, 0)
#include <vulkan/vulkan.h>
#pragma warning(pop)

static const VkPolygonMode vulkan_polygon_mode[] =
{
    VK_POLYGON_MODE_FILL,
    VK_POLYGON_MODE_LINE,
    VK_POLYGON_MODE_MAX_ENUM
};

static const VkCullModeFlags vulkan_cull_mode[] =
{
    VK_CULL_MODE_BACK_BIT,
    VK_CULL_MODE_FRONT_BIT,
    VK_CULL_MODE_NONE
};

static const VkPrimitiveTopology vulkan_primitive_topology[] =
{
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_PRIMITIVE_TOPOLOGY_LINE_LIST
};

static const VkFormat vulkan_format[] =
{
    // R
    VK_FORMAT_R8_UNORM,
    VK_FORMAT_R8_UINT,
    VK_FORMAT_R16_UNORM,
    VK_FORMAT_R16_UINT,
    VK_FORMAT_R16_SFLOAT,
    VK_FORMAT_R32_UINT,
    VK_FORMAT_R32_SFLOAT,
    // RG
    VK_FORMAT_R8G8_UNORM,
    VK_FORMAT_R16G16_SFLOAT,
    VK_FORMAT_R32G32_SFLOAT,
    // RGB
    VK_FORMAT_B10G11R11_UFLOAT_PACK32,
    VK_FORMAT_R32G32B32_SFLOAT,
    // RGBA
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_A2B10G10R10_UNORM_PACK32,
    VK_FORMAT_R16G16B16A16_UNORM,
    VK_FORMAT_R16G16B16A16_SNORM,
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    // Depth
    VK_FORMAT_D16_UNORM,
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    // Compressed
    VK_FORMAT_BC1_RGB_UNORM_BLOCK,
    VK_FORMAT_BC3_UNORM_BLOCK,
    VK_FORMAT_BC5_UNORM_BLOCK,
    VK_FORMAT_BC7_UNORM_BLOCK,
    VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
    // Surface
    VK_FORMAT_B8G8R8A8_UNORM,
    // Unknown
    VK_FORMAT_UNDEFINED
};

static const VkObjectType vulkan_object_type[] =
{
    VK_OBJECT_TYPE_FENCE,
    VK_OBJECT_TYPE_SEMAPHORE,
    VK_OBJECT_TYPE_SHADER_MODULE,
    VK_OBJECT_TYPE_SAMPLER,
    VK_OBJECT_TYPE_QUERY_POOL,
    VK_OBJECT_TYPE_DEVICE_MEMORY,
    VK_OBJECT_TYPE_BUFFER,
    VK_OBJECT_TYPE_COMMAND_BUFFER,
    VK_OBJECT_TYPE_COMMAND_POOL,
    VK_OBJECT_TYPE_IMAGE,
    VK_OBJECT_TYPE_IMAGE_VIEW,
    VK_OBJECT_TYPE_DESCRIPTOR_SET,
    VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
    VK_OBJECT_TYPE_PIPELINE,
    VK_OBJECT_TYPE_PIPELINE_LAYOUT,
    VK_OBJECT_TYPE_QUEUE,
    VK_OBJECT_TYPE_UNKNOWN
};

static const VkSamplerAddressMode vulkan_sampler_address_mode[] =
{
    VK_SAMPLER_ADDRESS_MODE_REPEAT,
    VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
    VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE
};

static const VkCompareOp vulkan_compare_operator[] =
{
    VK_COMPARE_OP_NEVER,
    VK_COMPARE_OP_LESS,
    VK_COMPARE_OP_EQUAL,
    VK_COMPARE_OP_LESS_OR_EQUAL,
    VK_COMPARE_OP_GREATER,
    VK_COMPARE_OP_NOT_EQUAL,
    VK_COMPARE_OP_GREATER_OR_EQUAL,
    VK_COMPARE_OP_ALWAYS
};

static const VkStencilOp vulkan_stencil_operation[] =
{
    VK_STENCIL_OP_KEEP,
    VK_STENCIL_OP_ZERO,
    VK_STENCIL_OP_REPLACE,
    VK_STENCIL_OP_INCREMENT_AND_CLAMP,
    VK_STENCIL_OP_DECREMENT_AND_CLAMP,
    VK_STENCIL_OP_INVERT,
    VK_STENCIL_OP_INCREMENT_AND_WRAP,
    VK_STENCIL_OP_DECREMENT_AND_WRAP
};

static const VkBlendFactor vulkan_blend_factor[] =
{
    VK_BLEND_FACTOR_ZERO,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_SRC_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    VK_BLEND_FACTOR_SRC_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    VK_BLEND_FACTOR_DST_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    VK_BLEND_FACTOR_DST_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
    VK_BLEND_FACTOR_CONSTANT_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
    VK_BLEND_FACTOR_SRC1_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
    VK_BLEND_FACTOR_SRC1_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
};

static const VkBlendOp vulkan_blend_operation[] =
{
    VK_BLEND_OP_ADD,
    VK_BLEND_OP_SUBTRACT,
    VK_BLEND_OP_REVERSE_SUBTRACT,
    VK_BLEND_OP_MIN,
    VK_BLEND_OP_MAX
};

static const VkFilter vulkan_filter[] =
{
    VK_FILTER_NEAREST,
    VK_FILTER_LINEAR
};

static const VkSamplerMipmapMode vulkan_mipmap_mode[] =
{
    VK_SAMPLER_MIPMAP_MODE_NEAREST,
    VK_SAMPLER_MIPMAP_MODE_LINEAR
};

static const VkImageLayout vulkan_image_layout[] =
{
    VK_IMAGE_LAYOUT_GENERAL,
    VK_IMAGE_LAYOUT_PREINITIALIZED,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    VK_IMAGE_LAYOUT_UNDEFINED
};

static const char* vkresult_to_string(const VkResult result)
{
    switch (result)
    {
        case VK_SUCCESS:                                            return "VK_SUCCESS";
        case VK_NOT_READY:                                          return "VK_NOT_READY";
        case VK_TIMEOUT:                                            return "VK_TIMEOUT";
        case VK_EVENT_SET:                                          return "VK_EVENT_SET";
        case VK_EVENT_RESET:                                        return "VK_EVENT_RESET";
        case VK_INCOMPLETE:                                         return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:                           return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:                         return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:                        return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:                                  return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:                            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:                            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:                        return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:                          return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:                          return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:                             return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:                         return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:                              return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_OUT_OF_POOL_MEMORY:                           return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:                      return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_SURFACE_LOST_KHR:                             return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:                     return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR:                                     return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:                              return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:                     return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT:                        return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV:                            return "VK_ERROR_INVALID_SHADER_NV";
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
        case VK_ERROR_FRAGMENTATION_EXT:                            return "VK_ERROR_FRAGMENTATION_EXT";
        case VK_ERROR_NOT_PERMITTED_EXT:                            return "VK_ERROR_NOT_PERMITTED_EXT";
        case VK_ERROR_INVALID_DEVICE_ADDRESS_EXT:                   return "VK_ERROR_INVALID_DEVICE_ADDRESS_EXT";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:          return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        case VK_ERROR_UNKNOWN:                                      return "VK_ERROR_UNKNOWN";
        case VK_RESULT_MAX_ENUM:                                    return "VK_RESULT_MAX_ENUM";
    }

    return "Unknown error code";
}

#define SP_ASSERT_VK_MSG(vk_result, text_message)          \
    if (vk_result != VK_SUCCESS)                           \
    {                                                      \
        Log::SetLogToFile(true);                           \
        SP_LOG_ERROR("%s", vkresult_to_string(vk_result)); \
        SP_ASSERT(false && text_message);                  \
    }
#endif

// RHI_Context
#include "RHI_Definitions.h"
namespace Spartan
{
    class SP_CLASS RHI_Context
    {
    public:
        // api specific
#       if defined(API_GRAPHICS_D3D12)
            static ID3D12Device* device;
        #elif defined(API_GRAPHICS_VULKAN)
            static VkInstance instance;
            static VkDevice device;
            static VkPhysicalDevice device_physical;
        #endif

        // api agnostic
        static std::string api_version_str;
        static std::string api_type_str;
        static RHI_Api_Type api_type;
    };
}

// Utilities
#if defined (API_GRAPHICS_D3D12)
    #include "D3D12/D3D12_Utility.h"
#endif

#endif // RUNTIME
