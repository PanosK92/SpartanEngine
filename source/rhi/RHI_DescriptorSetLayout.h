/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include "../core/SpartanObject.h"
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
        RHI_DescriptorSetLayout(const RHI_DescriptorSetLayout& source);
        ~RHI_DescriptorSetLayout();

        // binding api - O(1) slot lookup
        void SetConstantBuffer(uint32_t slot, RHI_Buffer* constant_buffer);
        void SetBuffer(uint32_t slot, RHI_Buffer* buffer);
        bool SetTexture(uint32_t slot, RHI_Texture* texture, uint32_t mip_index, uint32_t mip_range, uint32_t array_layer, RHI_Image_Layout layout, bool storage);
        bool ResolveTextureBindingOverlap(RHI_Texture* texture, uint32_t mip_index, uint32_t mip_range, uint32_t array_layer, bool storage);
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
        bool m_owns_resource = true;

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
