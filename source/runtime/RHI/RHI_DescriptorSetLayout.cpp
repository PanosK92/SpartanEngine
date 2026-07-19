/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "RHI_AccelerationStructure.h"
#include "RHI_Buffer.h"
#include "RHI_Texture.h"
#include "RHI_DescriptorSet.h"
#include "RHI_Device.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    RHI_DescriptorSetLayout::RHI_DescriptorSetLayout(const RHI_Descriptor* descriptors, size_t count, const char* name)
    {
        m_object_name = name;
        m_descriptors.reserve(count);
        m_bindings.reserve(count);

        // build slot -> index map for O(1) lookups
        for (size_t i = 0; i < count; ++i)
        {
            m_descriptors.push_back(descriptors[i]);
            m_bindings.emplace_back(); // default binding
            m_slot_to_index[descriptors[i].slot] = i;
        }

        // compute layout hash (immutable - based on slots and stages)
        for (const RHI_Descriptor& descriptor : m_descriptors)
        {
            m_layout_hash = rhi_hash_combine(m_layout_hash, static_cast<uint64_t>(descriptor.slot));
            m_layout_hash = rhi_hash_combine(m_layout_hash, static_cast<uint64_t>(descriptor.stage));
            m_layout_hash = rhi_hash_combine(m_layout_hash, static_cast<uint64_t>(descriptor.type));
            m_layout_hash = rhi_hash_combine(m_layout_hash, static_cast<uint64_t>(descriptor.as_array));
            m_layout_hash = rhi_hash_combine(m_layout_hash, static_cast<uint64_t>(descriptor.array_length));
            m_layout_hash = rhi_hash_combine(m_layout_hash, static_cast<uint64_t>(descriptor.struct_size));
        }

        CreateRhiResource();
    }

    RHI_DescriptorSetLayout::RHI_DescriptorSetLayout(const RHI_DescriptorSetLayout& source)
    {
        m_object_name    = source.m_object_name;
        m_rhi_resource   = source.m_rhi_resource;
        m_owns_resource  = false;
        m_descriptors    = source.m_descriptors;
        m_slot_to_index  = source.m_slot_to_index;
        m_layout_hash    = source.m_layout_hash;
        m_bindings.resize(m_descriptors.size());
    }

    RHI_DescriptorBinding* RHI_DescriptorSetLayout::FindBinding(uint32_t slot)
    {
        auto it = m_slot_to_index.find(slot);
        if (it != m_slot_to_index.end())
        {
            return &m_bindings[it->second];
        }
        return nullptr;
    }

    void RHI_DescriptorSetLayout::SetConstantBuffer(uint32_t slot, RHI_Buffer* constant_buffer)
    {
        uint32_t actual_slot = slot + rhi_shader_register_shift_b;
        if (RHI_DescriptorBinding* binding = FindBinding(actual_slot))
        {
            // get descriptor for validation
            const RHI_Descriptor& descriptor = m_descriptors[m_slot_to_index[actual_slot]];
            const uint64_t range              = constant_buffer->GetStride();
            const uint32_t dynamic_offset     = constant_buffer->GetOffset();

            if (binding->resource == constant_buffer && binding->range == range && binding->dynamic_offset == dynamic_offset)
            {
                return;
            }

            binding->resource       = constant_buffer;
            binding->range          = range;
            binding->dynamic_offset = dynamic_offset;

            SP_ASSERT_MSG(constant_buffer->GetStrideUnaligned() == descriptor.struct_size, "Size mismatch between CPU and GPU side constant buffer");
            SP_ASSERT_MSG(binding->dynamic_offset % binding->range == 0, "Incorrect dynamic offset");

            m_dirty = true;
        }
    }

    void RHI_DescriptorSetLayout::SetBuffer(uint32_t slot, RHI_Buffer* buffer)
    {
        uint32_t actual_slot = slot + rhi_shader_register_shift_u;
        RHI_DescriptorBinding* binding = FindBinding(actual_slot);
        if (!binding)
        {
            actual_slot = slot + rhi_shader_register_shift_t;
            binding = FindBinding(actual_slot);
        }

        if (binding)
        {
            const uint64_t range          = buffer->GetObjectSize();
            const uint32_t dynamic_offset = buffer->GetOffset();
            if (binding->resource == buffer && binding->range == range && binding->dynamic_offset == dynamic_offset)
            {
                return;
            }

            binding->resource       = buffer;
            binding->range          = range;
            binding->dynamic_offset = dynamic_offset;
            m_dirty = true;
        }
    }

    bool RHI_DescriptorSetLayout::SetTexture(uint32_t slot, RHI_Texture* texture, uint32_t mip_index, uint32_t mip_range, uint32_t array_layer, RHI_Image_Layout layout, bool storage)
    {
        SP_ASSERT(layout == RHI_Image_Layout::General || layout == RHI_Image_Layout::Shader_Read);

        uint32_t shift     = storage ? rhi_shader_register_shift_u : rhi_shader_register_shift_t;
        uint32_t actual_slot = slot + shift;

        if (RHI_DescriptorBinding* binding = FindBinding(actual_slot))
        {
            if (binding->resource == texture && binding->layout == layout && binding->mip == mip_index && binding->mip_range == mip_range && binding->array_layer == array_layer)
            {
                return true;
            }

            binding->resource    = texture;
            binding->layout      = layout;
            binding->mip         = mip_index;
            binding->mip_range   = mip_range;
            binding->array_layer = array_layer;
            m_dirty = true;
            return true;
        }
        return false;
    }

    bool RHI_DescriptorSetLayout::ResolveTextureBindingOverlap(RHI_Texture* texture, uint32_t mip_index, uint32_t mip_range, uint32_t array_layer, bool storage)
    {
        if (!texture)
        {
            return false;
        }

        bool overlap = false;
        const uint32_t clear_mip_start = mip_index == rhi_all_mips ? 0 : mip_index;
        const uint32_t clear_mip_count = mip_index == rhi_all_mips ? texture->GetMipCount() : (mip_range == 0 ? 1 : mip_range);
        const uint32_t clear_mip_end   = clear_mip_start + clear_mip_count;

        for (size_t i = 0; i < m_bindings.size(); i++)
        {
            RHI_DescriptorBinding& binding   = m_bindings[i];
            const RHI_Descriptor& descriptor = m_descriptors[i];
            const bool type_matches          = storage ? descriptor.type == RHI_Descriptor_Type::TextureStorage : descriptor.type == RHI_Descriptor_Type::Image;
            if (!type_matches || binding.resource != texture)
            {
                continue;
            }

            const uint32_t binding_mip_start = binding.mip == rhi_all_mips ? 0 : binding.mip;
            const uint32_t binding_mip_count = binding.mip == rhi_all_mips ? texture->GetMipCount() : (binding.mip_range == 0 ? 1 : binding.mip_range);
            const uint32_t binding_mip_end   = binding_mip_start + binding_mip_count;
            const bool mip_overlap           = clear_mip_start < binding_mip_end && binding_mip_start < clear_mip_end;
            const bool layer_overlap         = array_layer == rhi_all_mips || binding.array_layer == rhi_all_mips || array_layer == binding.array_layer;
            if (mip_overlap && layer_overlap && binding.layout != RHI_Image_Layout::General)
            {
                binding.layout = RHI_Image_Layout::General;
                m_dirty = true;
                overlap = true;
            }
        }
        return overlap;
    }

    void RHI_DescriptorSetLayout::SetAccelerationStructure(uint32_t slot, RHI_AccelerationStructure* tlas)
    {
        uint32_t actual_slot = slot + rhi_shader_register_shift_t;
        if (RHI_DescriptorBinding* binding = FindBinding(actual_slot))
        {
            const uint64_t range = tlas ? reinterpret_cast<uint64_t>(tlas->GetRhiResource()) : 0;
            if (binding->resource == tlas && binding->range == range)
            {
                return;
            }

            binding->resource = tlas;
            binding->range    = range;
            m_dirty = true;
        }
    }

    void RHI_DescriptorSetLayout::ClearBindings()
    {
        for (RHI_DescriptorBinding& binding : m_bindings)
        {
            binding.Reset();
        }
        m_dirty = true;
    }

    uint64_t RHI_DescriptorSetLayout::ComputeBindingHash() const
    {
        uint64_t hash = m_layout_hash;
        for (const RHI_DescriptorBinding& binding : m_bindings)
        {
            hash = rhi_hash_combine(hash, binding.GetHash());
        }
        return hash;
    }

    void* RHI_DescriptorSetLayout::GetOrCreateDescriptorSet()
    {
        // compute hash only if dirty
        if (m_dirty)
        {
            m_binding_hash = ComputeBindingHash();
            m_dirty = false;
        }

        // look up or create descriptor set
        unordered_map<uint64_t, RHI_DescriptorSet>& descriptor_sets = RHI_Device::GetDescriptorSets();
        auto it = descriptor_sets.find(m_binding_hash);

        if (it == descriptor_sets.end())
        {
            // build combined descriptors with bindings for descriptor set creation
            vector<RHI_DescriptorWithBinding> combined;
            combined.reserve(m_descriptors.size());

            for (size_t i = 0; i < m_descriptors.size(); ++i)
            {
                RHI_DescriptorWithBinding dwb;
                dwb.descriptor = m_descriptors[i];
                dwb.binding    = m_bindings[i];
                combined.push_back(dwb);
            }

            descriptor_sets[m_binding_hash] = RHI_DescriptorSet(combined, this, m_object_name.c_str());
            it = descriptor_sets.find(m_binding_hash);
        }

        it->second.MarkUsed(RHI_Device::GetDescriptorSetFrame());
        return it->second.GetResource();
    }

    void RHI_DescriptorSetLayout::GetDynamicOffsets(array<uint32_t, 10>* offsets, uint32_t* count)
    {
        *count = 0;
        for (size_t i = 0; i < m_descriptors.size(); ++i)
        {
            const RHI_Descriptor& descriptor = m_descriptors[i];
            if (descriptor.type == RHI_Descriptor_Type::StructuredBuffer || 
                descriptor.type == RHI_Descriptor_Type::ConstantBuffer)
            {
                (*offsets)[(*count)++] = m_bindings[i].dynamic_offset;
            }
        }
    }
}
