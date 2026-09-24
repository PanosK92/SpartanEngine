/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =================
#include "RHI_PipelineState.h"
#include "../core/SpartanObject.h"
//============================

namespace spartan
{
    class RHI_Pipeline : public SpartanObject
    {
    public:
        RHI_Pipeline() = default;
        RHI_Pipeline(RHI_PipelineState& pipeline_state, RHI_DescriptorSetLayout* descriptor_set_layout);
        ~RHI_Pipeline();

        RHI_PipelineState* GetState()            { return &m_state; }
        void* GetRhiResource() const             { return m_rhi_resource; }
        void* GetRhiResourceLayout() const       { return m_rhi_resource_layout; }
        void SetRhiResource(void* resource)      { m_rhi_resource = resource; }
        void SetRhiResourceLayout(void* layout)  { m_rhi_resource_layout = layout; }
        uint32_t GetPushConstantStages() const   { return m_push_constant_stages; }

    private:
        RHI_PipelineState m_state;
 
        // rhi
        void* m_rhi_resource          = nullptr;
        void* m_rhi_resource_layout   = nullptr;
        uint32_t m_push_constant_stages = 0;
    };
}
