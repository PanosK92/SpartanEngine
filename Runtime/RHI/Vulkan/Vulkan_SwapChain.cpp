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
#include "Vulkan_Helper.h"
#include <limits>
//=============================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	RHI_SwapChain::RHI_SwapChain(
		void* window_handle,
		const std::shared_ptr<RHI_Device>& rhi_device,
		unsigned int width,
		unsigned int height,
		const RHI_Format format			/*= Format_R8G8B8A8_UNORM */,
		RHI_Swap_Effect swap_effect		/*= Swap_Discard */,
		const unsigned long flags		/*= 0 */,
		const unsigned int buffer_count	/*= 1 */,
		void* render_pass				/*= nullptr */
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
		m_width			= width;
		m_height		= height;
		m_render_pass	= render_pass;
		m_window_handle	= window_handle;

		m_initialized = _Create();
	}

	RHI_SwapChain::~RHI_SwapChain()
	{
		auto device	= m_rhi_device->GetContext()->device;

		vulkan_helper::semaphore::destroy(device, m_semaphore_image_acquired);

		vkDestroySurfaceKHR(m_rhi_device->GetContext()->instance, static_cast<VkSurfaceKHR>(m_surface), nullptr);
		m_surface = nullptr;

		vkDestroySwapchainKHR(device, static_cast<VkSwapchainKHR>(m_swap_chain_view), nullptr);
		m_swap_chain_view = nullptr;

		for (auto& image_view : m_image_views) { vkDestroyImageView(device, static_cast<VkImageView>(image_view), nullptr); }
		m_image_views.clear();

		for (auto frame_buffer : m_frame_buffers) { vkDestroyFramebuffer(device, static_cast<VkFramebuffer>(frame_buffer), nullptr); }
		m_frame_buffers.clear();
	}

	bool RHI_SwapChain::Resize(const unsigned int width, const unsigned int height)
	{	
		if (m_width == width && m_height == height)
			return true;

		m_initialized =_Create();
		return m_initialized;
	}

	bool RHI_SwapChain::AcquireNextImage()
	{
		// Acquire next image
		auto result = vkAcquireNextImageKHR
		(
			m_rhi_device->GetContext()->device,
			static_cast<VkSwapchainKHR>(m_swap_chain_view),
			numeric_limits<uint64_t>::max(),
			static_cast<VkSemaphore>(m_semaphore_image_acquired),
			VK_NULL_HANDLE,
			&m_image_index
		);

		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to acquire next image, %s.", vulkan_helper::vk_result_to_string(result));
			return false;
		}

		return true;
	}

	bool RHI_SwapChain::Present(const RHI_Present_Mode mode, void* wait_semaphore)
	{	
		auto swap_chain							= static_cast<VkSwapchainKHR>(m_swap_chain_view);
		vector<VkSwapchainKHR> swap_chains		= { swap_chain };
		vector<VkSemaphore> semaphores_wait		= { static_cast<VkSemaphore>(wait_semaphore) };

		VkPresentInfoKHR present_info	= {};
		present_info.sType				= VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = static_cast<uint32_t>(semaphores_wait.size());
		present_info.pWaitSemaphores	= semaphores_wait.data();
		present_info.swapchainCount		= static_cast<uint32_t>(swap_chains.size());
		present_info.pSwapchains		= swap_chains.data();
		present_info.pImageIndices		= &m_image_index;		

		auto result = vkQueuePresentKHR(m_rhi_device->GetContext()->queue_present, &present_info);
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to present, %s.", vulkan_helper::vk_result_to_string(result));
			return false;
		}

		result = vkQueueWaitIdle(m_rhi_device->GetContext()->queue_present);
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to wait idle, %s.", vulkan_helper::vk_result_to_string(result));
			return false;
		}

		return true;
	}

	bool RHI_SwapChain::_Create()
	{
		RHI_Context* rhi_context = m_rhi_device->GetContext();

		// Create surface
		VkSurfaceKHR surface = nullptr;
		{
			VkWin32SurfaceCreateInfoKHR create_info = {};
			create_info.sType						= VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			create_info.hwnd						= static_cast<HWND>(m_window_handle);
			create_info.hinstance					= GetModuleHandle(nullptr);

			if (vkCreateWin32SurfaceKHR(rhi_context->instance, &create_info, nullptr, &surface) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create surface.");
				return false;
			}

			VkBool32 present_support = false;
			if (vkGetPhysicalDeviceSurfaceSupportKHR(rhi_context->device_physical, rhi_context->indices.graphics_family.value(), surface, &present_support))
			{
				LOG_ERROR("Failed to check for surface support by the device.");
				return false;
			}
			else if (!present_support)
			{
				LOG_ERROR("The device does not support this kind of surface.");
				return false;
			}
		}

		// Ensure device compatibility
		auto swap_chain_support = vulkan_helper::swap_chain::check_surface_compatibility(rhi_context, surface);
		if (!swap_chain_support.IsCompatible())
		{
			LOG_ERROR("Device is not surface compatible.");
			return false;
		}

		// Choose a suitable format
		auto format_selection = vulkan_helper::swap_chain::choose_format(vulkan_format[m_format], swap_chain_support.formats);

		// Swap chain
		VkSwapchainKHR swap_chain;
		{
			VkSwapchainCreateInfoKHR create_info = {};
			create_info.sType				= VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			create_info.surface				= surface;
			create_info.minImageCount		= m_buffer_count;
			create_info.imageFormat			= format_selection.format;
			create_info.imageColorSpace		= format_selection.colorSpace;
			create_info.imageExtent			= { static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height) };
			create_info.imageArrayLayers	= 1;
			create_info.imageUsage			= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			uint32_t queueFamilyIndices[] = { rhi_context->indices.graphics_family.value(), rhi_context->indices.present_family.value() };
			if (rhi_context->indices.graphics_family != rhi_context->indices.present_family)
			{
				create_info.imageSharingMode		 = VK_SHARING_MODE_CONCURRENT;
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
			create_info.presentMode		= vulkan_helper::swap_chain::choose_present_mode(swap_chain_support.present_modes);
			create_info.clipped			= VK_TRUE;
			create_info.oldSwapchain	= VK_NULL_HANDLE;

			if (vkCreateSwapchainKHR(rhi_context->device, &create_info, nullptr, &swap_chain) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create swap chain.");
				return false;
			}
		}

		// Images
		std::vector<VkImage> swap_chain_images;
		{
			uint32_t image_count;
			vkGetSwapchainImagesKHR(rhi_context->device, swap_chain, &image_count, nullptr);
			swap_chain_images.resize(image_count);
			vkGetSwapchainImagesKHR(rhi_context->device, swap_chain, &image_count, swap_chain_images.data());
		}

		// Image views
		std::vector<VkImageView> swap_chain_image_views;
		{
			swap_chain_image_views.resize(swap_chain_images.size());
			for (size_t i = 0; i < swap_chain_image_views.size(); i++)
			{
				VkImageViewCreateInfo createInfo			= {};
				createInfo.sType							= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				createInfo.image							= swap_chain_images[i];
				createInfo.viewType							= VK_IMAGE_VIEW_TYPE_2D;
				createInfo.format							= format_selection.format;
				createInfo.components.r						= VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.g						= VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.b						= VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.components.a						= VK_COMPONENT_SWIZZLE_IDENTITY;
				createInfo.subresourceRange.aspectMask		= VK_IMAGE_ASPECT_COLOR_BIT;
				createInfo.subresourceRange.baseMipLevel	= 0;
				createInfo.subresourceRange.levelCount		= 1;
				createInfo.subresourceRange.baseArrayLayer	= 0;
				createInfo.subresourceRange.layerCount		= 1;

				if (vkCreateImageView(rhi_context->device, &createInfo, nullptr, &swap_chain_image_views[i]) != VK_SUCCESS)
				{
					LOG_ERROR("Failed to create image view(s).");
					return false;
				}
			}
		}

		// Frame buffers
		vector<VkFramebuffer> frame_buffers;
		frame_buffers.resize(swap_chain_image_views.size());
		for (size_t i = 0; i < swap_chain_image_views.size(); i++)
		{
			VkImageView attachments[]				= { swap_chain_image_views[i] };
			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType					= VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass				= static_cast<VkRenderPass>(m_render_pass);
			framebufferInfo.attachmentCount			= 1;
			framebufferInfo.pAttachments			= attachments;
			framebufferInfo.width					= m_width;
			framebufferInfo.height					= m_height;
			framebufferInfo.layers					= 1;

			if (vkCreateFramebuffer(rhi_context->device, &framebufferInfo, nullptr, &frame_buffers[i]) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create frame buffer(s).");
				return false;
			}
		}

		m_semaphore_image_acquired	= vulkan_helper::semaphore::create(m_rhi_device->GetContext()->device);
		m_surface					= static_cast<void*>(surface);
		m_swap_chain_view			= static_cast<void*>(swap_chain);
		m_images					= vector<void*>(swap_chain_images.begin(), swap_chain_images.end());
		m_image_views				= vector<void*>(swap_chain_image_views.begin(), swap_chain_image_views.end());
		m_frame_buffers				= vector<void*>(frame_buffers.begin(), frame_buffers.end());

		return true;
	}
}
#endif