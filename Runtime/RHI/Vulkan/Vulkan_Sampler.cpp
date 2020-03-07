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

//= IMPLEMENTATION ===============
#ifdef API_GRAPHICS_VULKAN
#include "../RHI_Implementation.h"
//================================

//= INCLUDES ========================
#include "../RHI_Sampler.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
#include "../../Core/Context.h"
#include "../../Rendering/Renderer.h"
//===================================

namespace Spartan
{
	RHI_Sampler::RHI_Sampler(
		const std::shared_ptr<RHI_Device>& rhi_device,
		const RHI_Filter filter_min,							/*= Filter_Nearest*/
		const RHI_Filter filter_mag,							/*= Filter_Nearest*/
		const RHI_Sampler_Mipmap_Mode filter_mipmap,			/*= Sampler_Mipmap_Nearest*/
		const RHI_Sampler_Address_Mode sampler_address_mode,	/*= Sampler_Address_Wrap*/
		const RHI_Comparison_Function comparison_function,		/*= Texture_Comparison_Always*/
		const bool anisotropy_enabled,							/*= false*/
		const bool comparison_enabled							/*= false*/
	)
	{
		if (!rhi_device || !rhi_device->GetContextRhi()->device)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Save properties
		m_resource				= nullptr;
		m_rhi_device			= rhi_device;
		m_filter_min			= filter_min;
		m_filter_mag			= filter_mag;
		m_filter_mipmap			= filter_mipmap;
		m_sampler_address_mode	= sampler_address_mode;
		m_comparison_function	= comparison_function;
		m_anisotropy_enabled	= anisotropy_enabled;
		m_comparison_enabled	= comparison_enabled;

		VkSamplerCreateInfo sampler_info	= {};
		sampler_info.sType					= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler_info.magFilter				= vulkan_filter[filter_mag];
		sampler_info.minFilter				= vulkan_filter[filter_min];
		sampler_info.mipmapMode				= vulkan_mipmap_mode[filter_mipmap];
		sampler_info.addressModeU			= vulkan_sampler_address_mode[sampler_address_mode];
		sampler_info.addressModeV			= vulkan_sampler_address_mode[sampler_address_mode];
		sampler_info.addressModeW			= vulkan_sampler_address_mode[sampler_address_mode];
		sampler_info.anisotropyEnable		= anisotropy_enabled;
        sampler_info.maxAnisotropy          = m_rhi_device->GetContext()->GetSubsystem<Renderer>()->GetOptionValue<float>(Option_Value_Anisotropy);
		sampler_info.compareEnable			= comparison_enabled ? VK_TRUE : VK_FALSE;
		sampler_info.compareOp				= vulkan_compare_operator[comparison_function];
		sampler_info.borderColor			= VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	
		if (vkCreateSampler(rhi_device->GetContextRhi()->device, &sampler_info, nullptr, reinterpret_cast<VkSampler*>(&m_resource)) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create sampler");
		}
	}

	RHI_Sampler::~RHI_Sampler()
	{
		vkDestroySampler(m_rhi_device->GetContextRhi()->device, reinterpret_cast<VkSampler>(m_resource), nullptr);
	}
}
#endif
