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

//= INCLUDES =================
#include "RHI_PipelineCache.h"
#include "RHI_Device.h"
#include "RHI_Pipeline.h"
#include "RHI_PipelineState.h"
#include "..\Logging\Log.h"
//============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Pipeline* RHI_PipelineCache::GetPipeline(RHI_PipelineState& pipeline_state)
    {
        // Validate it
        if (!pipeline_state.IsValid())
        {
            LOG_ERROR("Invalid pipeline state");
            return nullptr;
        }

        // Compute a hash for it
        pipeline_state.ComputeHash();
        size_t hash = pipeline_state.GetHash();

        // If no pipeline exists for this state, create one
        if (m_cache.find(hash) == m_cache.end())
        {
            // Cache a new pipeline
            m_cache.emplace(make_pair(hash, move(make_shared<RHI_Pipeline>(m_rhi_device, pipeline_state))));
        }

        return m_cache[hash].get();
    }
}
