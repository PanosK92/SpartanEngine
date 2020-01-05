/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "RHI_DescriptorSet.h"
#include "RHI_Shader.h"
#include "RHI_Sampler.h"
#include "RHI_Texture.h"
#include "RHI_ConstantBuffer.h"
#include "RHI_Implementation.h"
#include "..\Utilities\Hash.h"
//============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_DescriptorSet::RHI_DescriptorSet(const std::shared_ptr<RHI_Device> rhi_device, const RHI_Shader* shader_vertex, const RHI_Shader* shader_pixel /*= nullptr*/)
    {
        m_rhi_device    = rhi_device;
        m_shader_vertex = shader_vertex;
        m_shader_pixel  = shader_pixel;

        ReflectShaders();
        SetDescriptorCapacity(m_descriptor_capacity);
    }

    void RHI_DescriptorSet::SetConstantBuffer(uint32_t slot, RHI_ConstantBuffer* constant_buffer)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if ((descriptor.type == RHI_Descriptor_ConstantBuffer || descriptor.type == RHI_Descriptor_ConstantBufferDynamic) && descriptor.slot == slot + m_rhi_device->GetContextRhi()->shader_shift_buffer)
            {
                bool different_id = descriptor.id != constant_buffer->GetId();

                // different offset needs to rebind the descriptor set with the actual offset, so we detect
                // it and mark it as dirty here, so it triggers a descriptor set bind in the command list
                bool different_offset = descriptor.offset != constant_buffer->GetOffset();

                m_descriptor_dirty = (different_id || different_offset) ? true : m_descriptor_dirty;

                // Update
                descriptor.id       = constant_buffer->GetId();
                descriptor.resource = constant_buffer->GetResource();
                descriptor.size     = constant_buffer->GetSize();
                descriptor.type     = constant_buffer->IsDynamic() ? RHI_Descriptor_ConstantBufferDynamic : RHI_Descriptor_ConstantBuffer;
                descriptor.offset   = constant_buffer->GetOffset();

                if (constant_buffer->IsDynamic())
                {
                    if (m_dynamic_offsets.empty())
                    {
                        m_dynamic_offsets.emplace_back(constant_buffer->GetOffset());
                    }
                    else
                    {
                        m_dynamic_offsets[0] = constant_buffer->GetOffset();
                    }
                }

                break;
            }
        }
    }

    void RHI_DescriptorSet::SetSampler(uint32_t slot, RHI_Sampler* sampler)
    {
        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.type == RHI_Descriptor_Sampler && descriptor.slot == slot + m_rhi_device->GetContextRhi()->shader_shift_sampler)
            {
                m_descriptor_dirty = descriptor.id != sampler->GetId() ? true : m_descriptor_dirty;

                // Update
                descriptor.id       = sampler->GetId();
                descriptor.resource = sampler->GetResource();

                break;
            }
        }
    }

    void RHI_DescriptorSet::SetTexture(uint32_t slot, RHI_Texture* texture)
    {
        if (!texture->IsSampled())
        {
            LOG_ERROR("Texture can't be used for sampling");
            return;
        }

        if (texture->GetLayout() == RHI_Image_Undefined || texture->GetLayout() == RHI_Image_Preinitialized)
        {
            LOG_ERROR("Texture has an invalid layout");
            return;
        }

        for (RHI_Descriptor& descriptor : m_descriptors)
        {
            if (descriptor.type == RHI_Descriptor_Texture && descriptor.slot == slot + m_rhi_device->GetContextRhi()->shader_shift_texture)
            {
                m_descriptor_dirty = descriptor.id != texture->GetId() ? true : m_descriptor_dirty;

                // Update
                descriptor.id       = texture->GetId();
                descriptor.resource = texture->GetResource_View();
                descriptor.layout   = texture->GetLayout();

                break;
            }
        }
    }

    void* RHI_DescriptorSet::GetResource_Set()
    {
        // Get the hash of the current descriptor blueprint
        size_t hash = GetDescriptorBlueprintHash(m_descriptors);

        // If the has is already present, then we don't need to update
        if (m_descriptor_sets.find(hash) != m_descriptor_sets.end())
        {
            if (m_descriptor_dirty)
            {
                m_descriptor_dirty = false;
                return m_descriptor_sets[hash];
            }

            return nullptr;
        }

        // Otherwise generate a new one and return that
        return CreateDescriptorSet(hash);
    }

    size_t RHI_DescriptorSet::GetDescriptorBlueprintHash(const std::vector<RHI_Descriptor>& descriptor_blueprint)
    {
        size_t hash = 0;

        for (const RHI_Descriptor& descriptor : m_descriptors)
        {
            Utility::Hash::hash_combine(hash, descriptor.slot);
            Utility::Hash::hash_combine(hash, descriptor.stage);
            Utility::Hash::hash_combine(hash, descriptor.id);
            Utility::Hash::hash_combine(hash, descriptor.size);
            Utility::Hash::hash_combine(hash, descriptor.offset);
            Utility::Hash::hash_combine(hash, static_cast<uint32_t>(descriptor.type));
            Utility::Hash::hash_combine(hash, static_cast<uint32_t>(descriptor.layout));
        }

        return hash;
    }

    void RHI_DescriptorSet::ReflectShaders()
    {
        m_descriptors.clear();

        if (!m_shader_vertex)
        {
            LOG_ERROR("Vertex shader is invalid");
            return;
        }

        // Wait for shader to compile
        while (m_shader_vertex->GetCompilationState() == Shader_Compilation_Compiling) {}

        // Get vertex shader descriptors
        m_descriptors = m_shader_vertex->GetDescriptors();

        // If there is a pixel shader, merge it's resources into our map as well
        if (m_shader_pixel)
        {
            while (m_shader_pixel->GetCompilationState() == Shader_Compilation_Compiling) {}
            for (const RHI_Descriptor& descriptor_reflected : m_shader_pixel->GetDescriptors())
            {
                // Assume that the descriptor has been created in the vertex shader and only try to update it's shader stage
                bool updated_existing = false;
                for (RHI_Descriptor& descriptor : m_descriptors)
                {
                    bool is_same_resource =
                        (descriptor.type == descriptor_reflected.type) &&
                        (descriptor.slot == descriptor_reflected.slot);

                    if ((descriptor.type == descriptor_reflected.type) && (descriptor.slot == descriptor_reflected.slot))
                    {
                        descriptor.stage |= descriptor_reflected.stage;
                        updated_existing = true;
                        break;
                    }
                }

                // If no updating took place, this descriptor is new, so add it
                if (!updated_existing)
                {
                    m_descriptors.emplace_back(descriptor_reflected);
                }
            }
        }
    }
}
