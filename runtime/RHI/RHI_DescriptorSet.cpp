/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES =================
#include "pch.h"
#include "RHI_DescriptorSet.h"
#include "RHI_Device.h"
//============================

namespace spartan
{
    spartan::RHI_DescriptorSet::RHI_DescriptorSet(const std::vector<RHI_Descriptor>& descriptors, RHI_DescriptorSetLayout* descriptor_set_layout, const char* name)
    {
        if (name)
        {
            m_object_name = name;
        }

        // allocate
        {
            RHI_Device::AllocateDescriptorSet(m_resource, descriptor_set_layout, descriptors);
            RHI_Device::SetResourceName(m_resource, RHI_Resource_Type::DescriptorSet, name);
        }

        Update(descriptors);
    }

    bool RHI_DescriptorSet::IsReferingToResource(void* resource)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.data == resource)
                return true;
        }

        return false;
    }
}
