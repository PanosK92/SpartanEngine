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

#pragma once

//= INCLUDES ============
#include <unordered_map>
#include "RHI_Pipeline.h"
//=======================

namespace Spartan
{
	class RHI_PipelineCache
	{
	public:
		RHI_PipelineCache(const std::shared_ptr<RHI_Device>& rhi_device) { m_rhi_device = rhi_device; }

		RHI_Pipeline* GetPipeline(const RHI_PipelineState& pipeline_state)
		{
			// If no pipeline matches the pipeline state, create one
			if (m_cache.find(pipeline_state) == m_cache.end()) 
			{
				m_cache[pipeline_state];
				m_cache[pipeline_state] = RHI_Pipeline(m_rhi_device, m_cache.find(pipeline_state)->first);
			}

			return &m_cache[pipeline_state];
		}

	private:
		std::unordered_map<RHI_PipelineState, RHI_Pipeline> m_cache;
		std::shared_ptr<RHI_Device> m_rhi_device;
	};
}