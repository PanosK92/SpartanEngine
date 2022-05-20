/*
Copyright(c) 2016-2022 Panos Karabelas

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

// Definition - DirectX 11
#if defined(API_GRAPHICS_D3D11)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma warning(push, 0) // Hide warnings which belong DirectX
#include <d3d11_4.h>
#pragma warning(pop)

static const D3D11_CULL_MODE d3d11_cull_mode[] =
{
    D3D11_CULL_NONE,
    D3D11_CULL_FRONT,
    D3D11_CULL_BACK
};

static const D3D11_FILL_MODE d3d11_polygon_mode[] =
{
    D3D11_FILL_SOLID,
    D3D11_FILL_WIREFRAME
};

static const D3D11_PRIMITIVE_TOPOLOGY d3d11_primitive_topology[] =
{
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
    D3D11_PRIMITIVE_TOPOLOGY_LINELIST
};

static const DXGI_FORMAT d3d11_format[] =
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
    DXGI_FORMAT_BC7_UNORM,
    DXGI_FORMAT_UNKNOWN,

    DXGI_FORMAT_UNKNOWN
};

static const D3D11_TEXTURE_ADDRESS_MODE d3d11_sampler_address_mode[] =
{
    D3D11_TEXTURE_ADDRESS_WRAP,
    D3D11_TEXTURE_ADDRESS_MIRROR,
    D3D11_TEXTURE_ADDRESS_CLAMP,
    D3D11_TEXTURE_ADDRESS_BORDER,
    D3D11_TEXTURE_ADDRESS_MIRROR_ONCE
};

static const D3D11_COMPARISON_FUNC d3d11_comparison_function[] =
{
    D3D11_COMPARISON_NEVER,
    D3D11_COMPARISON_LESS,
    D3D11_COMPARISON_EQUAL,
    D3D11_COMPARISON_LESS_EQUAL,
    D3D11_COMPARISON_GREATER,
    D3D11_COMPARISON_NOT_EQUAL,
    D3D11_COMPARISON_GREATER_EQUAL,
    D3D11_COMPARISON_ALWAYS
};

static const D3D11_STENCIL_OP d3d11_stencil_operation[] =
{
    D3D11_STENCIL_OP_KEEP,
    D3D11_STENCIL_OP_ZERO,
    D3D11_STENCIL_OP_REPLACE,
    D3D11_STENCIL_OP_INCR_SAT,
    D3D11_STENCIL_OP_DECR_SAT,
    D3D11_STENCIL_OP_INVERT,
    D3D11_STENCIL_OP_INCR,
    D3D11_STENCIL_OP_DECR
};

static const D3D11_BLEND d3d11_blend_factor[] =
{
    D3D11_BLEND_ZERO,
    D3D11_BLEND_ONE,
    D3D11_BLEND_SRC_COLOR,
    D3D11_BLEND_INV_SRC_COLOR,
    D3D11_BLEND_SRC_ALPHA,
    D3D11_BLEND_INV_SRC_ALPHA,
    D3D11_BLEND_DEST_ALPHA,
    D3D11_BLEND_INV_DEST_ALPHA,
    D3D11_BLEND_DEST_COLOR,
    D3D11_BLEND_INV_DEST_COLOR,
    D3D11_BLEND_SRC_ALPHA_SAT,
    D3D11_BLEND_BLEND_FACTOR,
    D3D11_BLEND_INV_BLEND_FACTOR,
    D3D11_BLEND_SRC1_COLOR,
    D3D11_BLEND_INV_SRC1_COLOR,
    D3D11_BLEND_SRC1_ALPHA,
    D3D11_BLEND_INV_SRC1_ALPHA
};

static const D3D11_BLEND_OP d3d11_blend_operation[] =
{
    D3D11_BLEND_OP_ADD,
    D3D11_BLEND_OP_SUBTRACT,
    D3D11_BLEND_OP_REV_SUBTRACT,
    D3D11_BLEND_OP_MIN,
    D3D11_BLEND_OP_MAX
};

#endif

// Definition - DirectX 12
#if defined(API_GRAPHICS_D3D12)
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma warning(push, 0) // Hide warnings which belong DirectX
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
    DXGI_FORMAT_BC7_UNORM,
    DXGI_FORMAT_UNKNOWN,

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

// Definition - Vulkan
#if defined(API_GRAPHICS_VULKAN) 
#pragma comment(lib, "vulkan-1.lib")
#define VK_USE_PLATFORM_WIN32_KHR
#pragma warning(push, 0) // Hide warnings which belong to Vulkan
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
    VK_CULL_MODE_NONE,
    VK_CULL_MODE_FRONT_BIT,
    VK_CULL_MODE_BACK_BIT,
    VK_CULL_MODE_FLAG_BITS_MAX_ENUM
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
    VK_FORMAT_A2R10G10B10_UNORM_PACK32,
    VK_FORMAT_R16G16B16A16_UNORM,
    VK_FORMAT_R16G16B16A16_SNORM,
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    // DEPTH
    VK_FORMAT_D16_UNORM,
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    // Compressed
    VK_FORMAT_BC7_UNORM_BLOCK,
    VK_FORMAT_ASTC_4x4_UNORM_BLOCK,

    VK_FORMAT_MAX_ENUM
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
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_GENERAL,
    VK_IMAGE_LAYOUT_PREINITIALIZED,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
};

#endif

// RHI_Context header dependencies
#include "RHI_Definition.h"
#if defined (API_GRAPHICS_VULKAN)
    #include "Vulkan/vk_mem_alloc.h"
    #include <vector>
    #include <unordered_map>
#endif

// RHI_Context
namespace Spartan
{
    struct RHI_Context
    {
        std::string api_version_str;

        #if defined(API_GRAPHICS_D3D11)
            RHI_Api_Type api_type                 = RHI_Api_Type::D3d11;
            ID3D11Device5* device                 = nullptr;
            ID3D11DeviceContext4* device_context  = nullptr;
            ID3DUserDefinedAnnotation* annotation = nullptr;
        #endif

        #if defined(API_GRAPHICS_D3D12)
            RHI_Api_Type api_type = RHI_Api_Type::D3d12;
            ID3D12Device* device  = nullptr;
        #endif

        #if defined(API_GRAPHICS_VULKAN)
            RHI_Api_Type api_type               = RHI_Api_Type::Vulkan;
            VkInstance instance                 = nullptr;
            VkPhysicalDevice device_physical    = nullptr;
            VkDevice device                     = nullptr;
            VkFormat surface_format             = VK_FORMAT_UNDEFINED;
            VkColorSpaceKHR surface_color_space = VK_COLOR_SPACE_MAX_ENUM_KHR;
            VmaAllocator allocator              = nullptr;
            std::unordered_map<uint64_t, VmaAllocation> allocations;

            // Extensions
            #ifdef DEBUG
                /*
                https://vulkan.lunarg.com/doc/view/1.2.135.0/mac/validation_layers.html

                VK_LAYER_KHRONOS_validation
                ===================================
                The main, comprehensive Khronos validation layer -- this layer encompasses the entire
                functionality of the layers listed below, and supersedes them. As the other layers
                are deprecated this layer should be used for all validation going forward.

                VK_EXT_debug_utils
                ==================
                Create a debug messenger which will pass along debug messages to an application supplied callback.
                Identify specific Vulkan objects using a name or tag to improve tracking.
                Identify specific sections within a VkQueue or VkCommandBuffer using labels to aid organization and offline analysis in external tools.

                */
                std::vector<const char*> validation_layers                      = { "VK_LAYER_KHRONOS_validation" };
                std::vector<VkValidationFeatureEnableEXT> validation_extensions = { VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT, VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT };
                std::vector<const char*> extensions_instance                    = { "VK_KHR_surface", "VK_KHR_win32_surface", "VK_EXT_debug_report", "VK_EXT_debug_utils", "VK_KHR_synchronization2"};
            #else
                std::vector<const char*> validation_layers                      = { };
                std::vector<VkValidationFeatureEnableEXT> validation_extensions = { };
                std::vector<const char*> extensions_instance                    = { "VK_KHR_surface", "VK_KHR_win32_surface" };
            #endif
                std::vector<const char*> extensions_device = { "VK_KHR_swapchain", "VK_EXT_memory_budget", "VK_EXT_depth_clip_enable", "VK_KHR_timeline_semaphore", "VK_KHR_dynamic_rendering" };

                bool InitialiseAllocator();
                void DestroyAllocator();
        #endif

        // Debugging
        #ifdef DEBUG
            bool debug       = true;
            bool gpu_markers = true;
            bool profiler    = true;
        #else
            bool debug       = false;
            bool gpu_markers = false;
            bool profiler    = true;
        #endif
    };
}

// HELPERS
#if defined(API_GRAPHICS_D3D11)
    #include "D3D11/D3D11_Utility.h"
#elif defined (API_GRAPHICS_D3D12)
    #include "D3D12/D3D12_Utility.h"
#elif defined (API_GRAPHICS_VULKAN)
    #include "Vulkan/Vulkan_Utility.h"
#endif

#endif // RUNTIME
