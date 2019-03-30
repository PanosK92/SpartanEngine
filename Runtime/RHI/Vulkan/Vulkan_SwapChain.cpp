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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES ==================
#include "../RHI_SwapChain.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
//=============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	RHI_SwapChain::RHI_SwapChain(
		void* window_handle,
		const std::shared_ptr<RHI_Device>& rhi_device,
		unsigned int width,
		unsigned int height,
		const RHI_Format format			/*= Format_R8G8B8A8_UNORM */,
		RHI_Swap_Effect swap_effect		/*= Swap_Discard */,
		const unsigned long flags		/*= 0 */,
		const unsigned int buffer_count	/*= 1 */
	)
	{
		const auto hwnd = static_cast<HWND>(window_handle);
		if (!hwnd || !rhi_device || !IsWindow(hwnd))
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Copy parameters
		m_format		= format;
		m_rhi_device	= rhi_device;
		m_flags			= flags;
		m_buffer_count	= buffer_count;

		RHI_Context* rhi_context = rhi_device->GetContext();

		// Create surface
		VkSurfaceKHR surface = nullptr;
		{
			VkWin32SurfaceCreateInfoKHR create_info = {};
			create_info.sType		= VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			create_info.hwnd		= hwnd;
			create_info.hinstance	= GetModuleHandle(nullptr);
	
			if (vkCreateWin32SurfaceKHR(rhi_context->instance, &create_info, nullptr, &surface) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create surface.");
				return;
			}
		}

		// Ensure device compatibility
		auto swap_chain_support = vulkan_helper::swap_chain::check_surface_compatibility(rhi_context, surface);
		if (!swap_chain_support.IsCompatible())
		{
			LOG_ERROR("Device is not surface compatible.");
			return;
		}

		// Create swap chain
		VkSwapchainKHR swap_chain;
		{
			auto present_mode	= vulkan_helper::swap_chain::choose_present_mode(swap_chain_support.present_modes);
			auto extent			= vulkan_helper::swap_chain::choose_extent(swap_chain_support.capabilities);

			VkSwapchainCreateInfoKHR create_info	= {};
			create_info.sType						= VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			create_info.surface						= surface;
			create_info.minImageCount				= buffer_count;
			create_info.imageFormat					= vulkan_format[m_format];
			create_info.imageColorSpace				= VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
			create_info.imageExtent					= extent;
			create_info.imageArrayLayers			= 1;
			create_info.imageUsage					= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			uint32_t queueFamilyIndices[] = { rhi_context->indices.graphics_family.value(), rhi_context->indices.present_family.value() };
			if (rhi_context->indices.graphics_family != rhi_context->indices.present_family)
			{
				create_info.imageSharingMode		= VK_SHARING_MODE_CONCURRENT;
				create_info.queueFamilyIndexCount	= 2;
				create_info.pQueueFamilyIndices		= queueFamilyIndices;
			}
			else
			{
				create_info.imageSharingMode		= VK_SHARING_MODE_EXCLUSIVE;
				create_info.queueFamilyIndexCount	= 0;
				create_info.pQueueFamilyIndices		= nullptr;
			}

			create_info.preTransform	= swap_chain_support.capabilities.currentTransform;
			create_info.compositeAlpha	= VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			create_info.presentMode		= present_mode;
			create_info.clipped			= VK_TRUE;
			create_info.oldSwapchain	= VK_NULL_HANDLE;

			if (vkCreateSwapchainKHR(rhi_context->device_context, &create_info, nullptr, &swap_chain) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create swap chain.");
			}
		}

		// Swap chain images
		std::vector<VkImage> swap_chain_images;
		{
			uint32_t image_count;			
			vkGetSwapchainImagesKHR(rhi_context->device_context, swap_chain, &image_count, nullptr);
			swap_chain_images.resize(image_count);
			vkGetSwapchainImagesKHR(rhi_context->device_context, swap_chain, &image_count, swap_chain_images.data());
		}

		// Swap chain image views
		std::vector<VkImageView> swap_chain_image_views;
		{
			swap_chain_image_views.resize(swap_chain_images.size());
			for (size_t i = 0; i < swap_chain_image_views.size(); i++)
			{
				VkImageViewCreateInfo createInfo			= {};
				createInfo.sType							= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				createInfo.image							= swap_chain_images[i];
				createInfo.viewType							= VK_IMAGE_VIEW_TYPE_2D;
				createInfo.format							= vulkan_format[m_format];
				createInfo.components.r						= VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.g						= VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.b						= VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.a						= VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.subresourceRange.aspectMask		= VK_IMAGE_ASPECT_COLOR_BIT;
				createInfo.subresourceRange.baseMipLevel	= 0;
				createInfo.subresourceRange.levelCount		= 1;
				createInfo.subresourceRange.baseArrayLayer	= 0;
				createInfo.subresourceRange.layerCount		= 1;

				if (vkCreateImageView(rhi_context->device_context, &createInfo, nullptr, &swap_chain_image_views[i]) != VK_SUCCESS) 
				{
					LOG_ERROR("Failed to create image views");
				}
			}
		}

		m_surface					= static_cast<void*>(surface);
		m_swap_chain				= static_cast<void*>(swap_chain);
		m_swap_chain_images			= vector<void*>(swap_chain_images.begin(), swap_chain_images.end());
		m_swap_chain_image_views	= vector<void*>(swap_chain_image_views.begin(), swap_chain_image_views.end());
		m_initialized				= true;
	}

	RHI_SwapChain::~RHI_SwapChain()
	{
		auto surface		= static_cast<VkSurfaceKHR>(m_surface);
		auto swapchain		= static_cast<VkSwapchainKHR>(m_swap_chain);
		auto rhi_context	= m_rhi_device->GetContext();

		vkDestroySurfaceKHR(rhi_context->instance, surface, nullptr);
		vkDestroySwapchainKHR(rhi_context->device_context, swapchain, nullptr);
		for (auto& image_view : m_swap_chain_image_views) 
		{
			vkDestroyImageView(rhi_context->device_context, (VkImageView)image_view, nullptr);
		}
	}

	bool RHI_SwapChain::Resize(const unsigned int width, const unsigned int height)
	{	
		return false;
	}

	bool RHI_SwapChain::Present(const RHI_Present_Mode mode) const
	{
		return false;
	}
}
#endif