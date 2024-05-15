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

#pragma once

//= INCLUDES =================
#include <memory>
#include "RHI_PipelineState.h"
#include "SpartanObject.h"
//============================

namespace Spartan
{
    class SP_CLASS RHI_Pipeline : public SpartanObject
    {
    public:
        RHI_Pipeline() = default;
        RHI_Pipeline(RHI_PipelineState& pipeline_state, RHI_DescriptorSetLayout* descriptor_set_layout);
        ~RHI_Pipeline();

        void* GetResource_Pipeline()          const { return m_resource_pipeline; }
        void* GetResource_PipelineLayout()    const { return m_resource_pipeline_layout; }
        RHI_PipelineState* GetPipelineState()       { return &m_state; }

    private:
        RHI_PipelineState m_state;
 
        // API
        void* m_resource_pipeline        = nullptr;
        void* m_resource_pipeline_layout = nullptr;
    };
}
