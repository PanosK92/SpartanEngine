/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==========================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_Device.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    RHI_DescriptorSetLayout::~RHI_DescriptorSetLayout()
    {
        // m_rhi_resource is a stable pointer encoding of m_layout_hash, no real com object to release
        // routing through the deletion queue keeps lifecycle parity with the vulkan path
        if (m_rhi_resource && m_owns_resource)
        {
            RHI_Device::DeletionQueueAdd(RHI_Resource_Type::DescriptorSetLayout, m_rhi_resource);
            m_rhi_resource = nullptr;
        }
    }

    void RHI_DescriptorSetLayout::CreateRhiResource()
    {
        // one bindless root signature serves every pso, so m_rhi_resource just holds the layout hash as a non-null token
        m_rhi_resource = reinterpret_cast<void*>(m_layout_hash != 0 ? m_layout_hash : static_cast<uint64_t>(0x1));
    }
}
