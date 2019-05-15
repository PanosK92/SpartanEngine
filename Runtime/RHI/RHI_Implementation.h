/*
Copyright(c) 2016-2019 Panos Karabelas

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

// DirectX 11
#if defined(API_GRAPHICS_D3D11)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#include <d3d11_4.h>

static const DXGI_SWAP_EFFECT d3d11_swap_effect[] =
{
	DXGI_SWAP_EFFECT_DISCARD,
	DXGI_SWAP_EFFECT_SEQUENTIAL,
	DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
	DXGI_SWAP_EFFECT_FLIP_DISCARD
};

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
	DXGI_FORMAT_R8_UNORM,
	DXGI_FORMAT_R16_UINT,
	DXGI_FORMAT_R16_FLOAT,
	DXGI_FORMAT_R32_UINT,
	DXGI_FORMAT_R32_FLOAT,
	DXGI_FORMAT_D32_FLOAT,
	DXGI_FORMAT_R32_TYPELESS,

	DXGI_FORMAT_R8G8_UNORM,
	DXGI_FORMAT_R16G16_FLOAT,
	DXGI_FORMAT_R32G32_FLOAT,

	DXGI_FORMAT_R32G32B32_FLOAT,

	DXGI_FORMAT_R8G8B8A8_UNORM,
	DXGI_FORMAT_R16G16B16A16_FLOAT,
	DXGI_FORMAT_R32G32B32A32_FLOAT
};

static const D3D11_TEXTURE_ADDRESS_MODE d3d11_sampler_address_mode[] =
{
	D3D11_TEXTURE_ADDRESS_WRAP,
	D3D11_TEXTURE_ADDRESS_MIRROR,
	D3D11_TEXTURE_ADDRESS_CLAMP,
	D3D11_TEXTURE_ADDRESS_BORDER,
	D3D11_TEXTURE_ADDRESS_MIRROR_ONCE
};

static const D3D11_COMPARISON_FUNC d3d11_compare_operator[] =
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

static const D3D11_FILTER d3d11_filter[] =
{
	D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
	D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
	D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
	D3D11_FILTER_MIN_MAG_MIP_POINT,
	D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
	D3D11_FILTER_MIN_MAG_MIP_LINEAR,
	D3D11_FILTER_ANISOTROPIC
};

static const D3D11_BLEND d3d11_blend_factor[] =
{
	D3D11_BLEND_ZERO,
	D3D11_BLEND_ONE,
	D3D11_BLEND_SRC_COLOR,
	D3D11_BLEND_INV_SRC_COLOR,
	D3D11_BLEND_SRC_ALPHA,
	D3D11_BLEND_INV_SRC_ALPHA
};

static const D3D11_BLEND_OP d3d11_blend_operation[] =
{
	D3D11_BLEND_OP_ADD,
	D3D11_BLEND_OP_SUBTRACT,
	D3D11_BLEND_OP_REV_SUBTRACT,
	D3D11_BLEND_OP_MIN,
	D3D11_BLEND_OP_MAX
};

namespace Spartan
{
	struct RHI_Context
	{
		ID3D11Device* device					= nullptr;
		ID3D11DeviceContext* device_context		= nullptr;
		ID3DUserDefinedAnnotation* annotation	= nullptr;
	};
}
#include "D3D11/D3D11_Common.h"
#endif 
// DIRECTX 11

// VULKAN
#if defined(API_GRAPHICS_VULKAN) 
#pragma comment(lib, "vulkan-1.lib")
#pragma comment(lib, "VkLayer_utils.lib")
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#pragma warning(push, 0) // Hide warnings which belong to Vulkan
#include <vulkan/vulkan.hpp>
#pragma warning(pop)
#include <optional>

static const VkPolygonMode vulkan_polygon_mode[] =
{
	VK_POLYGON_MODE_FILL,
	VK_POLYGON_MODE_LINE
};

static const VkCullModeFlags vulkan_cull_mode[] =
{	
	VK_CULL_MODE_NONE,
	VK_CULL_MODE_FRONT_BIT,
	VK_CULL_MODE_BACK_BIT
};

static const VkPrimitiveTopology vulkan_primitive_topology[] =
{
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	VK_PRIMITIVE_TOPOLOGY_LINE_LIST
};

static const VkFormat vulkan_format[] =
{
	VK_FORMAT_R8_UNORM,	
	VK_FORMAT_R16_UINT,	
	VK_FORMAT_R16_SFLOAT,	
	VK_FORMAT_R32_UINT,	
	VK_FORMAT_R32_SFLOAT,	
	VK_FORMAT_D32_SFLOAT,
	VK_FORMAT_D32_SFLOAT, // typeless ?
	
	VK_FORMAT_R8G8_UNORM,
	VK_FORMAT_R16G16_SFLOAT,
	VK_FORMAT_R32G32_SFLOAT,

	VK_FORMAT_R32G32B32_SFLOAT,

	VK_FORMAT_R8G8B8A8_UNORM,
	VK_FORMAT_R16G16B16A16_SFLOAT,
	VK_FORMAT_R32G32B32A32_SFLOAT
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

static const VkBlendFactor vulkan_blend_factor[] =
{
	VK_BLEND_FACTOR_ZERO,
	VK_BLEND_FACTOR_ONE,
	VK_BLEND_FACTOR_SRC_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	VK_BLEND_FACTOR_SRC_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
};

static const VkBlendOp vulkan_blend_operation[] =
{
	VK_BLEND_OP_ADD,
	VK_BLEND_OP_SUBTRACT,
	VK_BLEND_OP_REVERSE_SUBTRACT,
	VK_BLEND_OP_MIN,
	VK_BLEND_OP_MAX
};

static const VkDescriptorType vulkan_descriptor_type[] =
{
	VK_DESCRIPTOR_TYPE_SAMPLER,
	VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
};

struct SwapChainSupportDetails
{
	VkSurfaceCapabilitiesKHR capabilities = {};
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> present_modes;
	bool IsCompatible() { return !formats.empty() && !present_modes.empty(); }
};

struct QueueFamilyIndices
{
	std::optional<uint32_t> graphics_family;
	std::optional<uint32_t> present_family;
	std::optional<uint32_t> copy_family;
	bool IsComplete() const { return graphics_family.has_value() && present_family.has_value() && copy_family.has_value(); }
};

namespace Spartan
{
	struct RHI_Context
	{
		VkInstance instance							= nullptr;
		VkPhysicalDevice device_physical			= nullptr;
		VkDevice device								= nullptr;
		VkQueue queue_graphics						= nullptr;
		VkQueue queue_present						= nullptr;
		VkQueue queue_copy							= nullptr;
		VkDebugUtilsMessengerEXT callback_handle	= nullptr;
		QueueFamilyIndices indices;
		std::vector<const char*> validation_layers = { "VK_LAYER_KHRONOS_validation" };
		std::vector<const char*> extensions_device = { "VK_KHR_swapchain" };
		#ifdef DEBUG
			std::vector<const char*> extensions_instance = { "VK_KHR_surface", "VK_KHR_win32_surface", "VK_EXT_debug_utils" };
			bool validation_enabled = true;
		#else
			std::vector<const char*> extensions_instance = { "VK_KHR_surface", "VK_KHR_win32_surface" };
			bool validation_enabled = false;
		#endif

		static const uint32_t descriptor_count = 20; // maybe re-create the pool with double size every time the limit is reached ?
		static const uint32_t max_frames_in_flight = 2;
		static const uint32_t pool_max_constant_buffers_per_stage = 2;
		static const uint32_t pool_max_textures_per_stage = 2;
		static const uint32_t pool_max_samplers_per_stage = 2;
	};
}
#include "Vulkan/Vulkan_Common.h"
#endif // VULKAN

#endif // RUNTIME