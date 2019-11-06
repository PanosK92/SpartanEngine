/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ============
#include "RHI_Pipeline.h"
#include "RHI_Texture.h"
//=======================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void* RHI_Pipeline::SetConstantBuffer(uint32_t slot, RHI_ConstantBuffer* constant_buffer)
    {
        for (RHI_Descriptor& descriptor : m_descriptors_blueprint)
        {
            bool is_dirty =
                (descriptor.slot    == slot)                &&
                (descriptor.type    == Descriptor_Sampler)  &&
                (descriptor.id      != constant_buffer->GetId());

            if (is_dirty)
            {
                // Update with new data
                descriptor.id       = constant_buffer->GetId();
                descriptor.resource = constant_buffer->GetResource();
                descriptor.size     = constant_buffer->GetSize();

                // Get the hash now
                uint32_t hash = GetDescriptorBlueprintHash(m_descriptors_blueprint);

                // If we already have a descriptor set cached for this hash, return that
                if (m_descriptors_cache.find(hash) != m_descriptors_cache.end())
                {
                    m_descriptors_cache[hash];
                }
                // Otherwise generate a new one and return that
                else
                {
                    return UpdateDescriptorSet();
                }
            }
        }

        return nullptr;
    }

    void* RHI_Pipeline::SetSampler(uint32_t slot, RHI_Sampler* sampler)
    {
        for (RHI_Descriptor& descriptor : m_descriptors_blueprint)
        {

            bool is_dirty =
                (descriptor.slot    == slot)                &&
                (descriptor.type    == Descriptor_Sampler)  &&
                (descriptor.id      != sampler->GetId());

            if (is_dirty)
            {
                // Update with new data
                descriptor.id       = sampler->GetId();
                descriptor.resource = sampler->GetResource();

                // Get the hash now
                uint32_t hash = GetDescriptorBlueprintHash(m_descriptors_blueprint);

                // If we already have a descriptor set cached for this hash, return that
                if (m_descriptors_cache.find(hash) != m_descriptors_cache.end())
                {
                    m_descriptors_cache[hash];
                }
                // Otherwise generate a new one and return that
                else
                {
                    return UpdateDescriptorSet();
                }
            }
        }

        return nullptr;
    }

    void* RHI_Pipeline::SetTexture(uint32_t slot, RHI_Texture* texture)
    {
        for (RHI_Descriptor& descriptor : m_descriptors_blueprint)
        {

            bool is_dirty =
                (descriptor.slot    == slot)               &&
                (descriptor.type    == Descriptor_Texture) &&
                (descriptor.id      != texture->GetId());

            if (is_dirty)
            {
                // Update with new data
                descriptor.id       = texture->GetId();
                descriptor.resource = texture->GetResource_Texture();

                // Get the hash now
                uint32_t hash = GetDescriptorBlueprintHash(m_descriptors_blueprint);

                // If we already have a descriptor set cached for this hash, return that
                if (m_descriptors_cache.find(hash) != m_descriptors_cache.end())
                {
                    m_descriptors_cache[hash];
                }
                // Otherwise generate a new one and return that
                else
                {
                    return UpdateDescriptorSet();
                }
            }
        }

        return nullptr;
    }

    uint32_t RHI_Pipeline::GetDescriptorBlueprintHash(const std::vector<RHI_Descriptor>& descriptor_blueprint)
    {
        // Create a hash for each descriptor
        std::vector<uint32_t> hashes;
        hashes.reserve(m_descriptors_blueprint.size());
        for (const RHI_Descriptor& descriptor : m_descriptors_blueprint)
        {
            uint32_t a = static_cast<uint32_t>(descriptor.type);
            uint32_t b = descriptor.slot;
            uint32_t c = descriptor.id;

            // Szudzik's function - where a, b >= 0
            hashes.emplace_back(a >= b ? a * a + a + b : a + b * b);
        }

        // Create a hash from the hashes
        uint32_t uber_hash = 0;
        for (uint32_t hash : hashes)
        {
            uber_hash ^= hash + 0x9e3779b9 + (uber_hash << 6) + (uber_hash >> 2);
        }

        return uber_hash;
    }
}
