/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================
#include "pch.h"
#include "RHI_DescriptorSet.h"
#include "RHI_Device.h"
//============================

namespace spartan
{
    RHI_DescriptorSet::RHI_DescriptorSet(const std::vector<RHI_DescriptorWithBinding>& descriptors, RHI_DescriptorSetLayout* layout, const char* name)
    {
        if (name)
        {
            m_object_name = name;
        }

        // allocate vulkan descriptor set
        RHI_Device::AllocateDescriptorSet(m_resource, layout, descriptors);
        RHI_Device::SetResourceName(m_resource, RHI_Resource_Type::DescriptorSet, name);

        Update(descriptors);
    }

    bool RHI_DescriptorSet::IsReferingToResource(void* resource) const
    {
        for (const RHI_DescriptorWithBinding& desc : m_descriptors)
        {
            if (desc.binding.resource == resource)
            {
                return true;
            }
        }
        return false;
    }
}
