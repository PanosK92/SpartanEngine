/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES =======================
#include "pch.h"
#include "RHI_DescriptorSetLayout.h"
#include "RHI_ConstantBuffer.h"
#include "RHI_StructuredBuffer.h"
#include "RHI_Texture.h"
#include "RHI_DescriptorSet.h"
#include "RHI_Device.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_DescriptorSetLayout::RHI_DescriptorSetLayout(const vector<RHI_Descriptor>& descriptors, const string& name)
    {
        m_descriptors = descriptors;
        m_object_name = name;

        CreateRhiResource(m_descriptors);

        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(descriptor.slot));
            m_hash = rhi_hash_combine(m_hash, static_cast<uint64_t>(descriptor.stage));
        }
    }

    void RHI_DescriptorSetLayout::SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.slot == slot + rhi_shader_shift_register_b)
            {
                descriptor.data           = static_cast<void*>(constant_buffer); // needed for vkUpdateDescriptorSets()
                descriptor.range          = constant_buffer->GetStride();        // needed for vkUpdateDescriptorSets()
                descriptor.dynamic_offset = constant_buffer->GetOffset();        // needed for vkCmdBindDescriptorSets

                SP_ASSERT_MSG(constant_buffer->GetStructSize() == descriptor.struct_size, "Size mismatch between CPU and GPU side constant buffer");
                SP_ASSERT_MSG(descriptor.dynamic_offset % descriptor.range == 0,          "Incorrect dynamic offset");

                return;
            }
        }
    }

    void RHI_DescriptorSetLayout::SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.slot == slot + rhi_shader_shift_register_u)
            {
                descriptor.data           = static_cast<void*>(structured_buffer);
                descriptor.range          = structured_buffer->GetStride();
                descriptor.dynamic_offset = structured_buffer->GetOffset();

                return;
            }
        }
    }

    void RHI_DescriptorSetLayout::SetSampler(const uint32_t slot, RHI_Sampler* sampler)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.slot == slot + rhi_shader_shift_register_s)
            {
                descriptor.data = static_cast<void*>(sampler);

                return;
            }
        }
    }

    void RHI_DescriptorSetLayout::SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index, const uint32_t mip_range)
    {
        bool mip_specified      = mip_index != rhi_all_mips;
        RHI_Image_Layout layout = texture->GetLayout(mip_specified ? mip_index : 0);

        SP_ASSERT(layout == RHI_Image_Layout::General || layout == RHI_Image_Layout::Shader_Read || layout == RHI_Image_Layout::Shader_Read_Depth);

        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            bool is_storage = layout == RHI_Image_Layout::General;
            uint32_t shift  = is_storage ? rhi_shader_shift_register_u : rhi_shader_shift_register_t;

            if (descriptor.slot == (slot + shift))
            {
                descriptor.data      = static_cast<void*>(texture);
                descriptor.layout    = layout;
                descriptor.mip       = mip_index;
                descriptor.mip_range = mip_range;

                return;
            }
        }
    }

    void RHI_DescriptorSetLayout::ClearDescriptorData()
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            descriptor.data           = nullptr;
            descriptor.mip            = 0;
            descriptor.mip_range      = 0;
            descriptor.dynamic_offset = 0;
        }
    }

    RHI_DescriptorSet* RHI_DescriptorSetLayout::GetDescriptorSet()
    {
        RHI_DescriptorSet* descriptor_set = nullptr;

        // integrate descriptor data into the hash (anything that can change)
        uint64_t hash = m_hash;
        for (const RHI_Descriptor& descriptor : m_descriptors)
        {
            hash = rhi_hash_combine(hash, reinterpret_cast<uint64_t>(descriptor.data));
            hash = rhi_hash_combine(hash, static_cast<uint64_t>(descriptor.mip));
            hash = rhi_hash_combine(hash, static_cast<uint64_t>(descriptor.mip_range));
        }

        // if we don't have a descriptor set to match that state, create one
        unordered_map<uint64_t, RHI_DescriptorSet>& descriptor_sets = RHI_Device::GetDescriptorSets();
        const auto it = descriptor_sets.find(hash);
        if (it == descriptor_sets.end()) // create descriptor set
        {
            descriptor_sets[hash] = RHI_DescriptorSet(m_descriptors, this, m_object_name.c_str());
            descriptor_set        = &descriptor_sets[hash];
        }
        else // retrieve the existing one
        {
            descriptor_set = &it->second;
        }
    
        return descriptor_set;
    }

    void RHI_DescriptorSetLayout::GetDynamicOffsets(std::array<uint32_t, 10>* offsets, uint32_t* count)
    {
        // offsets should be ordered by the binding slots in the descriptor
        // set layouts, so m_descriptors should already be sorted by slot

        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.type == RHI_Descriptor_Type::StructuredBuffer || descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
            {
                (*offsets)[(*count)++] = descriptor.dynamic_offset;
            }
        }
    }
}
