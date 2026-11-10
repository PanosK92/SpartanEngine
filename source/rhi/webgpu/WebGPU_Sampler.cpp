/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "../RHI_Sampler.h"
#include "../RHI_Device.h"
#include "WebGPU_Internal.h"
//=============================

namespace spartan
{
    void RHI_Sampler::CreateResource()
    {
        const WGPUAddressMode address_mode = webgpu_sampler_address_mode[static_cast<uint32_t>(m_sampler_address_mode)];

        WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
        descriptor.label                 = webgpu::label(m_object_name.c_str());
        descriptor.addressModeU          = address_mode;
        descriptor.addressModeV          = address_mode;
        descriptor.addressModeW          = address_mode;
        descriptor.magFilter             = m_filter_mag == RHI_Filter::Linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        descriptor.minFilter             = m_filter_min == RHI_Filter::Linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        descriptor.mipmapFilter          = m_filter_mipmap == RHI_Filter::Linear ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
        descriptor.lodMinClamp           = 0.0f;
        descriptor.lodMaxClamp           = 32.0f;

        // anisotropy needs every filter linear, webgpu rejects it otherwise
        const bool all_linear       = m_filter_mag == RHI_Filter::Linear && m_filter_min == RHI_Filter::Linear && m_filter_mipmap == RHI_Filter::Linear;
        descriptor.maxAnisotropy    = static_cast<uint16_t>(all_linear ? std::clamp(static_cast<uint32_t>(m_anisotropy), 1u, 16u) : 1u);
        if (m_comparison_enabled)
        {
            descriptor.compare = webgpu_comparison_function[static_cast<uint32_t>(m_comparison_function)];
        }

        m_rhi_resource = wgpuDeviceCreateSampler(RHI_Context::device, &descriptor);
        SP_ASSERT_MSG(m_rhi_resource != nullptr, "Failed to create sampler");
    }

    RHI_Sampler::~RHI_Sampler()
    {
        if (m_rhi_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Sampler, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }
}
