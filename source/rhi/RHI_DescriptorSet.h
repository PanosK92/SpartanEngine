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
//================================

namespace spartan
{
    class RHI_DescriptorSet : public SpartanObject
    {
    public:
        RHI_DescriptorSet() = default;
        RHI_DescriptorSet(const std::vector<RHI_DescriptorWithBinding>& descriptors, RHI_DescriptorSetLayout* layout, const char* name);
        ~RHI_DescriptorSet() = default;

        bool IsReferingToResource(void* resource) const;
        void* GetResource() const { return m_resource; }
        uint64_t GetLastUsedFrame() const { return m_last_used_frame; }
        void MarkUsed(uint64_t frame)     { m_last_used_frame = frame; }

    private:
        void Update(const std::vector<RHI_DescriptorWithBinding>& descriptors);

        std::vector<RHI_DescriptorWithBinding> m_descriptors;
        void* m_resource          = nullptr;
        uint64_t m_last_used_frame = 0;
    };
}
