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
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	inline bool _Create
	(
		const std::shared_ptr<RHI_Device>& rhi_device,
		uint32_t width,
		uint32_t height,
		uint32_t buffer_count,
		RHI_Format format,
		RHI_Present_Mode present_mode,
		void* window_handle,
		void* render_pass,
		void*& surface_out,
		void*& swap_chain_view_out,
		vector<void*>& image_views_out,
		vector<void*>& frame_buffers_out,
		vector<void*>& semaphores_image_acquired_out
	)
	{
		auto rhi_context		= rhi_device->GetContext();
		auto device				= rhi_device->GetContext()->device;
		auto device_physical	= rhi_device->GetContext()->device_physical;

		// Create surface
		VkSurfaceKHR surface = nullptr;
		{
			VkWin32SurfaceCreateInfoKHR create_info = {};
			create_info.sType						= VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			create_info.hwnd						= static_cast<HWND>(window_handle);
			create_info.hinstance					= GetModuleHandle(nullptr);

			auto result = vkCreateWin32SurfaceKHR(rhi_context->instance, &create_info, nullptr, &surface);
			if (result != VK_SUCCESS)
			{
				LOGF_ERROR("Failed to create Win32 surface, %s.", Vulkan_Common::result_to_string(result));
				return false;
			}

			VkBool32 present_support = false;
			result = vkGetPhysicalDeviceSurfaceSupportKHR(device_physical, rhi_context->indices.graphics_family.value(), surface, &present_support);
			if (result != VK_SUCCESS)
			{
				LOGF_ERROR("Failed to check for surface support by the device, %s.", Vulkan_Common::result_to_string(result));
				return false;
			}
			else if (!present_support)
			{
				LOG_ERROR("The device does not support this kind of surface.");
				return false;
			}
		}

		// Ensure device compatibility
		auto swap_chain_support = Vulkan_Common::swap_chain::check_surface_compatibility(rhi_device.get(), surface);
		if (!swap_chain_support.IsCompatible())
		{
			LOG_ERROR("Device is not surface compatible.");
			return false;
		}

		auto extent = Vulkan_Common::swap_chain::choose_extent(swap_chain_support.capabilities, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
		auto format_selection = Vulkan_Common::swap_chain::choose_format(vulkan_format[format], swap_chain_support.formats);

		// Swap chain
		VkSwapchainKHR swap_chain;
		{
			VkSwapchainCreateInfoKHR create_info	= {};
			create_info.sType						= VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			create_info.surface						= surface;
			create_info.minImageCount				= buffer_count;
			create_info.imageFormat					= format_selection.format;
			create_info.imageColorSpace				= format_selection.colorSpace;
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
			create_info.presentMode		= Vulkan_Common::swap_chain::choose_present_mode(static_cast<VkPresentModeKHR>(present_mode), swap_chain_support.present_modes);
			create_info.clipped			= VK_TRUE;
			create_info.oldSwapchain	= VK_NULL_HANDLE;

			auto result = vkCreateSwapchainKHR(device, &create_info, nullptr, &swap_chain);
			if (result != VK_SUCCESS)
			{
				LOGF_ERROR("Failed to create swap chain, %s.", Vulkan_Common::result_to_string(result));
				return false;
			}
		}

		// Images
		vector<VkImage> swap_chain_images;
		{
			uint32_t image_count;
			vkGetSwapchainImagesKHR(device, swap_chain, &image_count, nullptr);
			swap_chain_images.resize(image_count);
			vkGetSwapchainImagesKHR(device, swap_chain, &image_count, swap_chain_images.data());
		}

		// Image views
		bool swizzle = true;
		vector<VkImageView> swap_chain_image_views;
		{
			swap_chain_image_views.resize(swap_chain_images.size());
			for (size_t i = 0; i < swap_chain_image_views.size(); i++)
			{
				if (!Vulkan_Common::image_view::create(rhi_device, swap_chain_images[i], swap_chain_image_views[i], format_selection.format, swizzle))
				{
					LOG_ERROR("Failed to create image view");
					return false;
				}
			}
		}

		// Frame buffers
		vector<VkFramebuffer> frame_buffers;
		frame_buffers.resize(swap_chain_image_views.size());
		for (auto i = 0; i < swap_chain_image_views.size(); i++)
		{
			vector<VkImageView> attachments = { swap_chain_image_views[i] };
			VkFramebufferCreateInfo framebufferInfo		= {};
			framebufferInfo.sType						= VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass					= static_cast<VkRenderPass>(render_pass);
			framebufferInfo.attachmentCount				= static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments				= attachments.data();
			framebufferInfo.width						= extent.width;
			framebufferInfo.height						= extent.height;
			framebufferInfo.layers						= 1;

			auto result = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frame_buffers[i]);
			if (result != VK_SUCCESS)
			{
				LOGF_ERROR("Failed to create frame buffer(s), %s.", Vulkan_Common::result_to_string(result));
				return false;
			}
		}
	
		surface_out			= static_cast<void*>(surface);
		swap_chain_view_out	= static_cast<void*>(swap_chain);
		image_views_out		= vector<void*>(swap_chain_image_views.begin(), swap_chain_image_views.end());
		frame_buffers_out	= vector<void*>(frame_buffers.begin(), frame_buffers.end());

		for (unsigned int i = 0; i < buffer_count; i++)
		{
			semaphores_image_acquired_out.emplace_back(Vulkan_Common::semaphore::create(rhi_device));
		}

		return true;
	}

	inline void _Destroy(
		const std::shared_ptr<RHI_Device>& rhi_device,
		void*& surface,
		void*& swap_chain_view,
		vector<void*>& image_views,
		vector<void*>& frame_buffers,
		vector<void*>& semaphores_image_acquired
	)
	{
		for (auto& semaphore : semaphores_image_acquired)
		{
			Vulkan_Common::semaphore::destroy(rhi_device, semaphore);
		}
		semaphores_image_acquired.clear();

		for (auto frame_buffer : frame_buffers) { vkDestroyFramebuffer(rhi_device->GetContext()->device, static_cast<VkFramebuffer>(frame_buffer), nullptr); }
		frame_buffers.clear();

		for (auto& image_view : image_views) { vkDestroyImageView(rhi_device->GetContext()->device, static_cast<VkImageView>(image_view), nullptr); }
		image_views.clear();

		if (swap_chain_view)
		{
			vkDestroySwapchainKHR(rhi_device->GetContext()->device, static_cast<VkSwapchainKHR>(swap_chain_view), nullptr);
			swap_chain_view = nullptr;
		}

		if (surface)
		{
			vkDestroySurfaceKHR(rhi_device->GetContext()->instance, static_cast<VkSurfaceKHR>(surface), nullptr);
			surface = nullptr;	
		}
	}

	RHI_SwapChain::RHI_SwapChain(
		void* window_handle,
		const std::shared_ptr<RHI_Device>& rhi_device,
		unsigned int width,
		unsigned int height,
		const RHI_Format format			/*= Format_R8G8B8A8_UNORM */,
		RHI_Present_Mode present_mode	/*= Present_Off */,
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
		m_buffer_count	= buffer_count;
		m_width			= width;
		m_height		= height;
		m_render_pass	= render_pass;
		m_window_handle	= window_handle;
		m_present_mode	= present_mode;

		m_initialized = _Create
		(
			m_rhi_device,
			m_width,
			m_height,
			m_buffer_count,
			m_format,
			m_present_mode,
			m_window_handle,
			m_render_pass,
			m_surface,
			m_swap_chain_view,
			m_image_views,
			m_frame_buffers,
			m_semaphores_image_acquired
		);
	}

	RHI_SwapChain::~RHI_SwapChain()
	{
		_Destroy
		(
			m_rhi_device,
			m_surface,
			m_swap_chain_view,
			m_image_views,
			m_frame_buffers,
			m_semaphores_image_acquired
		);
	}

	bool RHI_SwapChain::Resize(const unsigned int width, const unsigned int height)
	{	
		// Only resize if needed
		if (m_width == width && m_height == height)
			return true;

		// Save new dimensions
		m_width		= width;
		m_height	= height;

		// Destroy previous swap chain
		_Destroy
		(
			m_rhi_device,
			m_surface,
			m_swap_chain_view,
			m_image_views,
			m_frame_buffers,
			m_semaphores_image_acquired
		);

		// Create the swap chain with the new dimensions
		m_initialized = _Create
		(
			m_rhi_device,
			m_width,
			m_height,
			m_buffer_count,
			m_format,
			m_present_mode,
			m_window_handle,
			m_render_pass,
			m_surface,
			m_swap_chain_view,
			m_image_views,
			m_frame_buffers,
			m_semaphores_image_acquired
		);

		return m_initialized;
	}

	bool RHI_SwapChain::AcquireNextImage()
	{
		// Make index that always matches the m_image_index after vkAcquireNextImageKHR.
		// This is so getting semaphores and fences can be done by using m_image_index.
		auto index = m_first_run ? 0 : (m_image_index + 1) % m_buffer_count;

		// Acquire next image
		auto result = vkAcquireNextImageKHR
		(
			m_rhi_device->GetContext()->device,
			static_cast<VkSwapchainKHR>(m_swap_chain_view),
			0xFFFFFFFFFFFFFFFF,
			static_cast<VkSemaphore>(m_semaphores_image_acquired[index]),
			VK_NULL_HANDLE,
			&m_image_index
		);

		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to acquire next image, %s.", Vulkan_Common::result_to_string(result));
			return false;
		}

		m_first_run = false;
		return true;
	}

	bool RHI_SwapChain::Present(void* semaphore_render_finished)
	{	
		vector<VkSwapchainKHR> swap_chains	= { static_cast<VkSwapchainKHR>(m_swap_chain_view) };
		vector<VkSemaphore> semaphores_wait	= { static_cast<VkSemaphore>(semaphore_render_finished) };

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
			LOGF_ERROR("Failed to present, %s.", Vulkan_Common::result_to_string(result));
		}

		return result == VK_SUCCESS;
	}
}
#endif