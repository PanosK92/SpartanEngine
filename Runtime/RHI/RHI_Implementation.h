/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==================
#include "../Core/EngineDefs.h"
//=============================

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
	DXGI_FORMAT_R16G16B16A16_FLOAT,
	DXGI_FORMAT_R32G32B32A32_FLOAT,
    // Depth
    DXGI_FORMAT_D32_FLOAT,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT,

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
	VK_FORMAT_R16G16B16A16_SFLOAT,
	VK_FORMAT_R32G32B32A32_SFLOAT,
    // DEPTH
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,

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

static const VkDescriptorType vulkan_descriptor_type[] =
{
	VK_DESCRIPTOR_TYPE_SAMPLER,
	VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
    VK_DESCRIPTOR_TYPE_MAX_ENUM
};

static const VkImageLayout vulkan_image_layout[] =
{
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_GENERAL,
    VK_IMAGE_LAYOUT_PREINITIALIZED,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
};
#endif

// HEADER DEPENDENCIES
#if defined (API_GRAPHICS_D3D11)
    #include <stdint.h>
#elif defined (API_GRAPHICS_VULKAN)
    #include <vector>
#endif

// RHI CONTEXT - All
namespace Spartan
{
    struct RHI_Context
    {
        #if defined(API_GRAPHICS_D3D11)
            ID3D11Device* device                    = nullptr;
            ID3D11DeviceContext* device_context     = nullptr;
            ID3DUserDefinedAnnotation* annotation   = nullptr;
        #endif

        #if defined(API_GRAPHICS_VULKAN)
            VkInstance instance                             = nullptr;
            VkPhysicalDevice device_physical                = nullptr;
            VkDevice device                                 = nullptr;
            VkPhysicalDeviceProperties device_properties    = {};
            VkPhysicalDeviceFeatures device_features        = {};
            VkFormat surface_format                         = VK_FORMAT_UNDEFINED;
            VkColorSpaceKHR surface_color_space             = VK_COLOR_SPACE_MAX_ENUM_KHR;

            // Extensions
            #ifdef DEBUG
                /*
                https://vulkan.lunarg.com/doc/view/1.1.108.0/mac/validation_layers.html

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
                std::vector<const char*> extensions_device      = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME };
                std::vector<const char*> validation_layers      = { "VK_LAYER_KHRONOS_validation" };
                std::vector<const char*> extensions_instance    = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
            #else
                std::vector<const char*> extensions_device      = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME };
                std::vector<const char*> validation_layers      = { };
                std::vector<const char*> extensions_instance    = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
            #endif
        #endif

        // Debugging
        #ifdef DEBUG
            bool debug    = true;
            bool markers  = true;
            bool profiler = true;
        #else
            bool debug    = false;
            bool markers  = false;
            bool profiler = true;
        #endif

        // Shader resource slot shifts
        static const uint32_t shader_shift_rw_buffer    = 000;
        static const uint32_t shader_shift_buffer       = 100;
        static const uint32_t shader_shift_texture      = 200;
        static const uint32_t shader_shift_sampler      = 300;

        // Descriptor set limits
        static const uint32_t descriptor_max_constant_buffers           = 10;
        static const uint32_t descriptor_max_constant_buffers_dynamic   = 10;
        static const uint32_t descriptor_max_samplers                   = 10;
        static const uint32_t descriptor_max_textures                   = 10;

        // Device limits
        uint32_t max_texture_dimension_2d   = 16384;
        uint32_t max_msaa_level             = 0;

        // Queues
        void* queue_graphics            = nullptr;
        void* queue_compute             = nullptr;
        void* queue_transfer            = nullptr;
        uint32_t queue_graphics_index   = 0;
        uint32_t queue_transfer_index   = 0;
        uint32_t queue_compute_index    = 0;
    };
}

// HELPERS
#if defined(API_GRAPHICS_D3D11)
    #include "D3D11/D3D11_Common.h"
#elif defined (API_GRAPHICS_VULKAN)
    #include "Vulkan/Vulkan_Common.h"
#endif

#endif // RUNTIME
