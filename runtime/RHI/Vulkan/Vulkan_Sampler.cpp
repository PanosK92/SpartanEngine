/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_Sampler.h"
#include "../RHI_Device.h"
//================================

namespace spartan
{
    void RHI_Sampler::CreateResource()
    {
        VkSamplerCreateInfo sampler_info = {};
        sampler_info.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter           = vulkan_filter[static_cast<uint32_t>(m_filter_mag)];
        sampler_info.minFilter           = vulkan_filter[static_cast<uint32_t>(m_filter_min)];
        sampler_info.mipmapMode          = vulkan_mipmap_mode[static_cast<uint32_t>(m_filter_mipmap)];
        sampler_info.addressModeU        = vulkan_sampler_address_mode[static_cast<uint32_t>(m_sampler_address_mode)];
        sampler_info.addressModeV        = vulkan_sampler_address_mode[static_cast<uint32_t>(m_sampler_address_mode)];
        sampler_info.addressModeW        = vulkan_sampler_address_mode[static_cast<uint32_t>(m_sampler_address_mode)];
        sampler_info.anisotropyEnable    = m_anisotropy != 0;
        sampler_info.maxAnisotropy       = m_anisotropy;
        sampler_info.compareEnable       = m_comparison_enabled ? VK_TRUE : VK_FALSE;
        sampler_info.compareOp           = vulkan_compare_operator[static_cast<uint32_t>(m_comparison_function)];
        sampler_info.borderColor         = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
        sampler_info.mipLodBias          = m_mip_lod_bias;
        sampler_info.minLod              = 0.0f;
        sampler_info.maxLod              = FLT_MAX;
    
        SP_ASSERT_VK(vkCreateSampler(RHI_Context::device, &sampler_info, nullptr, reinterpret_cast<VkSampler*>(&m_rhi_resource)));
    }

    RHI_Sampler::~RHI_Sampler()
    {
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Sampler, m_rhi_resource);
    }
}
