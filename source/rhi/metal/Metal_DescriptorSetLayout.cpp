/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_Device.h"
//=============================

namespace spartan
{
    RHI_DescriptorSetLayout::~RHI_DescriptorSetLayout()
    {
        if (m_rhi_resource && m_owns_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::DescriptorSetLayout, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }

    void RHI_DescriptorSetLayout::CreateRhiResource()
    {
        // no native layout object on metal, the hash is a non null token like on d3d12
        m_rhi_resource = reinterpret_cast<void*>(m_layout_hash != 0 ? m_layout_hash : static_cast<uint64_t>(0x1));
    }
}
