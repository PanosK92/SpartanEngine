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

//= INCLUDES =================
#include "RHI_PipelineState.h"
#include "../Core/SpartanObject.h"
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

    private:
        RHI_PipelineState m_state;
 
        // rhi
        void* m_rhi_resource        = nullptr;
        void* m_rhi_resource_layout = nullptr;
    };
}
