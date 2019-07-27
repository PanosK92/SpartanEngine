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
#include "TimeBlock.h"
#include "../Logging/Log.h"
#include "../RHI/RHI_Device.h"
//============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	TimeBlock::~TimeBlock()
	{
		if (m_query)
		{
			m_rhi_device->ProfilingReleaseQuery(m_query);
			m_rhi_device->ProfilingReleaseQuery(m_query_start);
			m_rhi_device->ProfilingReleaseQuery(m_query_end);
		}

		Clear();
		m_query			= nullptr;
		m_query_start	= nullptr;
		m_query_end		= nullptr;
	}

	void TimeBlock::Begin(const string& name, bool profile_cpu /*= false*/, bool profile_gpu /*= false*/, const TimeBlock* parent /*= nullptr*/, const shared_ptr<RHI_Device>& rhi_device /*= nullptr*/)
	{
		m_name			= name;
		m_parent		= parent;
		m_tree_depth	= FindTreeDepth(this);
		m_rhi_device	= rhi_device.get();

		if (profile_cpu)
		{
			start = chrono::high_resolution_clock::now();
			m_profiling_cpu = true;
		}

		if (profile_gpu)
		{
			// Create required queries
			if (!m_query)
			{
				rhi_device->ProfilingCreateQuery(&m_query, Query_Timestamp_Disjoint);
				rhi_device->ProfilingCreateQuery(&m_query_start, Query_Timestamp);
				rhi_device->ProfilingCreateQuery(&m_query_end, Query_Timestamp);
			}

			// Get time stamp
			rhi_device->ProfilingQueryStart(m_query);
			rhi_device->ProfilingGetTimeStamp(m_query_start);

			m_profiling_gpu = true;
		}

		m_is_complete = false;
		m_has_started = true;
	}

	void TimeBlock::End(const shared_ptr<RHI_Device>& rhi_device /*= nullptr*/)
	{
		if (!m_has_started)
		{
			LOG_WARNING("TimeBlock::Start() hasn't been called, ignoring time block.");
			return;
		}

		if (m_profiling_cpu)
		{
			end = chrono::high_resolution_clock::now();
			chrono::duration<double, milli> ms = end - start;
			m_duration_cpu = static_cast<float>(ms.count());
		}

		if (m_profiling_gpu)
		{
			if (m_query)
			{
				// Get time stamp
				rhi_device->ProfilingGetTimeStamp(m_query_end);
				rhi_device->ProfilingGetTimeStamp(m_query);
			}
			else
			{
				LOG_ERROR_INVALID_INTERNALS();
			}
		}

		m_is_complete = true;
		m_has_started = false;
	}

	void TimeBlock::OnFrameEnd(const shared_ptr<RHI_Device>& rhi_device)
	{
		if (!m_query)
			return;

		m_duration_gpu = rhi_device->ProfilingGetDuration(m_query, m_query_start, m_query_end);
	}

	void TimeBlock::Clear()
	{
		m_name.clear();
		m_parent		= nullptr;
		m_tree_depth	= 0;
		m_is_complete	= false;
		m_has_started	= false;
		m_duration_cpu	= 0.0f;
		m_duration_gpu	= 0.0f;
		m_profiling_cpu = false;
		m_profiling_gpu = false;
	}

	uint32_t TimeBlock::FindTreeDepth(const TimeBlock* time_block, uint32_t depth /*= 0*/)
	{
		if (time_block->GetParent())
			depth =	FindTreeDepth(time_block->GetParent(), ++depth);

		return depth;
	}
}
