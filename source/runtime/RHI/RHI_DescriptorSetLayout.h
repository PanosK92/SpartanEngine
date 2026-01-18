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

#pragma once

//= INCLUDES =====================
#include "../Core/SpartanObject.h"
#include "RHI_Descriptor.h"
#include <vector>
#include <unordered_map>
//================================

namespace spartan
{
    class RHI_DescriptorSetLayout : public SpartanObject
    {
    public:
        RHI_DescriptorSetLayout() = default;
        RHI_DescriptorSetLayout(const RHI_Descriptor* descriptors, size_t count, const char* name);
        ~RHI_DescriptorSetLayout();

        // binding api - O(1) slot lookup
        void SetConstantBuffer(uint32_t slot, RHI_Buffer* constant_buffer);
        void SetBuffer(uint32_t slot, RHI_Buffer* buffer);
        void SetTexture(uint32_t slot, RHI_Texture* texture, uint32_t mip_index, uint32_t mip_range);
        void SetAccelerationStructure(uint32_t slot, RHI_AccelerationStructure* tlas);

        // dynamic offsets for bound buffers
        void GetDynamicOffsets(std::array<uint32_t, 10>* offsets, uint32_t* count);

        // state management
        void ClearBindings();
        void* GetOrCreateDescriptorSet();
        bool IsDirty() const { return m_dirty; }

        // accessors
        const std::vector<RHI_Descriptor>& GetDescriptors() const { return m_descriptors; }
        const std::vector<RHI_DescriptorBinding>& GetBindings() const { return m_bindings; }
        uint64_t GetLayoutHash() const { return m_layout_hash; }
        void* GetRhiResource() const { return m_rhi_resource; }

    private:
        void CreateRhiResource();
        RHI_DescriptorBinding* FindBinding(uint32_t slot);
        uint64_t ComputeBindingHash() const;

        // vulkan descriptor set layout
        void* m_rhi_resource = nullptr;

        // layout info (immutable after construction)
        std::vector<RHI_Descriptor> m_descriptors;
        std::unordered_map<uint32_t, size_t> m_slot_to_index; // slot -> index in m_descriptors
        uint64_t m_layout_hash = 0;

        // binding state (mutable)
        std::vector<RHI_DescriptorBinding> m_bindings; // parallel to m_descriptors
        uint64_t m_binding_hash = 0;
        bool m_dirty = true;
    };
}
