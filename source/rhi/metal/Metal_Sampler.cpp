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
#include "Metal_Internal.h"
//=============================

namespace spartan
{
    void RHI_Sampler::CreateResource()
    {
        const MTL::SamplerAddressMode address_mode = metal_sampler_address_mode[static_cast<uint32_t>(m_sampler_address_mode)];

        MTL::SamplerDescriptor* descriptor = MTL::SamplerDescriptor::alloc()->init();
        descriptor->setMinFilter(m_filter_min == RHI_Filter::Linear ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
        descriptor->setMagFilter(m_filter_mag == RHI_Filter::Linear ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
        descriptor->setMipFilter(m_filter_mipmap == RHI_Filter::Linear ? MTL::SamplerMipFilterLinear : MTL::SamplerMipFilterNearest);
        descriptor->setSAddressMode(address_mode);
        descriptor->setTAddressMode(address_mode);
        descriptor->setRAddressMode(address_mode);
        descriptor->setMaxAnisotropy(std::max(1u, static_cast<uint32_t>(m_anisotropy)));
        descriptor->setLodBias(m_mip_lod_bias);
        descriptor->setBorderColor(MTL::SamplerBorderColorTransparentBlack);
        if (m_comparison_enabled)
        {
            descriptor->setCompareFunction(metal_comparison_function[static_cast<uint32_t>(m_comparison_function)]);
        }
        // bindless samplers are referenced by resource id from argument buffers
        descriptor->setSupportArgumentBuffers(true);

        m_rhi_resource = RHI_Context::device->newSamplerState(descriptor);
        descriptor->release();
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
