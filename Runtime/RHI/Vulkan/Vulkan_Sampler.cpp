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

//= INCLUDES =================
#include "../RHI_Sampler.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
//============================

namespace Spartan
{
	RHI_Sampler::RHI_Sampler(
		const std::shared_ptr<RHI_Device>& rhi_device,
		const RHI_Texture_Filter filter						/*= Texture_Sampler_Anisotropic*/,
		const RHI_Sampler_Address_Mode sampler_address_mode	/*= Sampler_Address_Wrap*/,
		const RHI_Comparison_Function comparison_function	/*= Texture_Comparison_Always*/
	)
	{	
		m_rhi_device = rhi_device;

		VkSamplerCreateInfo sampler_info	= {};
		sampler_info.sType					= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler_info.magFilter				= VK_FILTER_LINEAR;					// FIX THIS, as in d3d11 all flags are merged into one
		sampler_info.minFilter				= VK_FILTER_LINEAR;					// FIX THIS
		sampler_info.mipmapMode				= VK_SAMPLER_MIPMAP_MODE_LINEAR;	// FIX THIS
		sampler_info.addressModeU			= vulkan_sampler_address_mode[sampler_address_mode];
		sampler_info.addressModeV			= vulkan_sampler_address_mode[sampler_address_mode];
		sampler_info.addressModeW			= vulkan_sampler_address_mode[sampler_address_mode];
		sampler_info.compareEnable			= comparison_function != Comparison_Never ? VK_TRUE : VK_FALSE;
		sampler_info.compareOp				= vulkan_compare_operator[comparison_function];
		sampler_info.borderColor			= VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	
		VkSampler sampler = nullptr;
		if (vkCreateSampler(rhi_device->GetContext()->device, &sampler_info, nullptr, &sampler) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create sampler");
		}

		m_buffer_view = static_cast<void*>(sampler);
	}

	RHI_Sampler::~RHI_Sampler()
	{
		vkDestroySampler(m_rhi_device->GetContext()->device, static_cast<VkSampler>(m_buffer_view), nullptr);
	}
}
#endif