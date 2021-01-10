/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ===================
#include "Spartan.h"
#include "RHI_PipelineCache.h"
#include "RHI_Texture.h"
#include "RHI_Pipeline.h"
#include "RHI_SwapChain.h"
#include "RHI_DescriptorCache.h"
//==============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Pipeline* RHI_PipelineCache::GetPipeline(RHI_CommandList* cmd_list, RHI_PipelineState& pipeline_state, void* descriptor_set_layout)
    {
        // Validate it
        if (!pipeline_state.IsValid())
        {
            LOG_ERROR("Invalid pipeline state");
            return nullptr;
        }

        // Render target layout transitions
        pipeline_state.TransitionRenderTargetLayouts(cmd_list);

        // Compute a hash for it
        uint32_t hash = pipeline_state.ComputeHash();

        // If no pipeline exists for this state, create one
        auto it = m_cache.find(hash);
        if (it == m_cache.end())
        {
            // Cache a new pipeline
            it = m_cache.emplace(make_pair(hash, move(make_shared<RHI_Pipeline>(m_rhi_device, pipeline_state, descriptor_set_layout)))).first;
            LOG_INFO("A new pipeline has been created.");
        }

        return it->second.get();
    }
}
