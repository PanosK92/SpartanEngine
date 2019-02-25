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
#include "Vulkan_Helper.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES ==================
#include "../RHI_SwapChain.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
#include <windows.h>
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
		const auto hwnd	= static_cast<HWND>(window_handle);

		if (!hwnd || !rhi_device || !IsWindow(hwnd))
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		m_format		= format;
		m_rhi_device	= rhi_device;
		m_flags			= flags;
		m_buffer_count	= buffer_count;

		// Surface
		VkSurfaceKHR_T* surface = nullptr;
		{	
			VkWin32SurfaceCreateInfoKHR create_info = {};
			create_info.sType		= VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			create_info.hwnd		= hwnd;
			create_info.hinstance	= GetModuleHandle(nullptr);
		
			const auto instance_ptr = m_rhi_device->GetInstance<VkInstance_T>();
			if (vkCreateWin32SurfaceKHR(instance_ptr, &create_info, nullptr, &surface) != VK_SUCCESS)
			{
				LOG_ERROR("Failed to create surface.");
			}
		}
		
		m_surface = static_cast<void*>(surface);
	}

	RHI_SwapChain::~RHI_SwapChain()
	{
		const auto instance	= m_rhi_device->GetInstance<VkInstance_T>();
		const auto surface	= static_cast<VkSurfaceKHR_T*>(m_surface);
		vkDestroySurfaceKHR(instance, surface, nullptr);
	}

	bool RHI_SwapChain::Resize(const unsigned int width, const unsigned int height)
	{	
		return false;
	}

	bool RHI_SwapChain::SetAsRenderTarget() const
	{
		return false;
	}

	bool RHI_SwapChain::Clear(const Vector4& color) const
	{
		return false;
	}

	bool RHI_SwapChain::Present(const RHI_Present_Mode mode) const
	{
		return false;
	}
}
#endif