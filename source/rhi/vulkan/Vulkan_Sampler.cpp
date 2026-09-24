/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
