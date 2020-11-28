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

//= INCLUDES ========================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_Sampler.h"
#include "../RHI_Device.h"
#include "../../Rendering/Renderer.h"
//===================================

namespace Spartan
{
    void RHI_Sampler::CreateResource()
    {
        VkSamplerCreateInfo sampler_info    = {};
        sampler_info.sType                  = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter              = vulkan_filter[m_filter_mag];
        sampler_info.minFilter              = vulkan_filter[m_filter_min];
        sampler_info.mipmapMode             = vulkan_mipmap_mode[m_filter_mipmap];
        sampler_info.addressModeU           = vulkan_sampler_address_mode[m_sampler_address_mode];
        sampler_info.addressModeV           = vulkan_sampler_address_mode[m_sampler_address_mode];
        sampler_info.addressModeW           = vulkan_sampler_address_mode[m_sampler_address_mode];
        sampler_info.anisotropyEnable       = m_anisotropy_enabled;
        sampler_info.maxAnisotropy          = m_rhi_device->GetContext()->GetSubsystem<Renderer>()->GetOptionValue<float>(Option_Value_Anisotropy);
        sampler_info.compareEnable          = m_comparison_enabled ? VK_TRUE : VK_FALSE;
        sampler_info.compareOp              = vulkan_compare_operator[m_comparison_function];
        sampler_info.borderColor            = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        sampler_info.mipLodBias             = 0.0f;
        sampler_info.minLod                 = 0.0f;
        sampler_info.maxLod                 = FLT_MAX;
    
        if (vkCreateSampler(m_rhi_device->GetContextRhi()->device, &sampler_info, nullptr, reinterpret_cast<VkSampler*>(&m_resource)) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create sampler");
        }
    }

    RHI_Sampler::~RHI_Sampler()
    {
        vkDestroySampler(m_rhi_device->GetContextRhi()->device, reinterpret_cast<VkSampler>(m_resource), nullptr);
    }
}
