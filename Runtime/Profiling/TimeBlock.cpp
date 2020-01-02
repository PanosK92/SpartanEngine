/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ======================
#include "TimeBlock.h"
#include "../Logging/Log.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_CommandList.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    uint32_t TimeBlock::m_max_tree_depth = 0;

	TimeBlock::~TimeBlock()
	{
		if (m_query_disjoint)
		{
			RHI_CommandList::Gpu_QueryRelease(m_query_disjoint);
			RHI_CommandList::Gpu_QueryRelease(m_query_start);
			RHI_CommandList::Gpu_QueryRelease(m_query_end);
		}

		OnFrameStart();
		m_query_disjoint    = nullptr;
		m_query_start	    = nullptr;
		m_query_end		    = nullptr;
	}

	void TimeBlock::Begin(const char* name, TimeBlock_Type type, const TimeBlock* parent /*= nullptr*/, RHI_CommandList* cmd_list /*= nullptr*/, const shared_ptr<RHI_Device>& rhi_device /*= nullptr*/)
	{
		m_name			= name;
		m_parent		= parent;
		m_tree_depth	= FindTreeDepth(this);
		m_rhi_device	= rhi_device.get();
        m_cmd_list      = cmd_list;
        m_type          = type;

        m_max_tree_depth = Math::Max(m_max_tree_depth, m_tree_depth);

		if (type == TimeBlock_Cpu)
		{
			start = chrono::high_resolution_clock::now();
			m_profiling = true;
		}
		else if (type == TimeBlock_Gpu)
		{
			// Create required queries
			if (!m_query_disjoint)
			{
				RHI_CommandList::Gpu_QueryCreate(m_rhi_device, &m_query_disjoint, RHI_Query_Timestamp_Disjoint);
				RHI_CommandList::Gpu_QueryCreate(m_rhi_device, &m_query_start, RHI_Query_Timestamp);
				RHI_CommandList::Gpu_QueryCreate(m_rhi_device, &m_query_end, RHI_Query_Timestamp);
			}

            if (cmd_list)
            {
                cmd_list->Timestamp_Start(m_query_disjoint, m_query_start);
            }

            m_profiling = true;
		}

		m_is_complete = false;
		m_has_started = true;
	}

	void TimeBlock::End()
	{
		if (!m_has_started)
		{
			LOG_WARNING("TimeBlock::Start() hasn't been called, ignoring time block.");
			return;
		}

		if (m_type == TimeBlock_Cpu)
		{
			end = chrono::high_resolution_clock::now();
			chrono::duration<double, milli> ms = end - start;
			m_duration = static_cast<float>(ms.count());
		}
		else if (m_type == TimeBlock_Gpu)
		{
            if (m_cmd_list)
            {
                m_cmd_list->Timestamp_End(m_query_disjoint, m_query_end);
                m_duration = m_cmd_list->Timestamp_GetDuration(m_query_disjoint, m_query_start, m_query_end);
            }
		}

		m_is_complete = true;
		m_has_started = false;
	}

    void TimeBlock::OnFrameStart()
	{
        // Reset
        m_name              = nullptr;
		m_parent		    = nullptr;
		m_tree_depth	    = 0;
		m_is_complete	    = false;
		m_has_started	    = false;
		m_duration	        = 0.0f;
		m_profiling         = false;
        m_max_tree_depth    = 0;
        m_type              = TimeBlock_Undefined;
	}

	uint32_t TimeBlock::FindTreeDepth(const TimeBlock* time_block, uint32_t depth /*= 0*/)
	{
		if (time_block->GetParent())
			depth =	FindTreeDepth(time_block->GetParent(), ++depth);

		return depth;
	}
}
