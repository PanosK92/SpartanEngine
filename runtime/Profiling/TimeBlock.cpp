/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "pch.h"
#include "TimeBlock.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_CommandList.h"
#include "../Rendering/Renderer.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    uint32_t TimeBlock::m_max_tree_depth = 0;

    TimeBlock::~TimeBlock()
    {
        Reset();
    }

    void TimeBlock::Begin(const uint32_t id, const char* name, TimeBlockType type, const TimeBlock* parent /*= nullptr*/, RHI_CommandList* cmd_list /*= nullptr*/)
    {
        m_id             = id;
        m_name           = name;
        m_parent         = parent;
        m_tree_depth     = FindTreeDepth(this);
        m_type           = type;
        m_max_tree_depth = Math::Helper::Max(m_max_tree_depth, m_tree_depth);
        if (cmd_list)
        {
            m_cmd_list   = cmd_list;
            m_rhi_device = cmd_list->GetContext()->GetSubsystem<Renderer>()->GetRhiDevice().get();
        }

        if (type == TimeBlockType::Cpu)
        {
            m_start = chrono::high_resolution_clock::now();
        }
        else if (type == TimeBlockType::Gpu)
        {
            // Create required queries
            if (!m_query_start)
            {
                m_rhi_device->QueryCreate(&m_query_start, RHI_Query_Type::Timestamp);
                m_rhi_device->QueryCreate(&m_query_end, RHI_Query_Type::Timestamp);
            }

            cmd_list->BeginTimestamp(m_query_start);
        }
    }

    void TimeBlock::End()
    {
        if (m_type == TimeBlockType::Cpu)
        {
            m_end = chrono::high_resolution_clock::now();
        }
        else if (m_type == TimeBlockType::Gpu)
        {
            m_cmd_list->EndTimestamp(m_query_end);
        }

        m_is_complete = true;
    }

    void TimeBlock::ComputeDuration(const uint32_t pass_index)
    {
        // Ensure this time block has completed.
        SP_ASSERT(m_is_complete);

        if (m_type == TimeBlockType::Cpu)
        {
            const chrono::duration<double, milli> ms = m_end - m_start;
            m_duration = static_cast<float>(ms.count());
        }
        else if (m_type == TimeBlockType::Gpu)
        {
            m_duration = m_cmd_list->GetTimestampDuration(m_query_start, m_query_end, pass_index);
        }
    }

    void TimeBlock::Reset()
    {
        m_name           = nullptr;
        m_parent         = nullptr;
        m_tree_depth     = 0;
        m_duration       = 0.0f;
        m_max_tree_depth = 0;
        m_type           = TimeBlockType::Undefined;
        m_is_complete    = false;

        if (m_query_start != nullptr && m_query_end != nullptr)
        {
            m_rhi_device->QueryRelease(m_query_start);
            m_rhi_device->QueryRelease(m_query_end);
        }
    }

    void TimeBlock::ClearGpuObjects()
    {
        m_query_start = nullptr;
        m_query_end   = nullptr;
    }

    uint32_t TimeBlock::FindTreeDepth(const TimeBlock* time_block, uint32_t depth /*= 0*/)
    {
        if (time_block && time_block->GetParent())
        {
            depth = FindTreeDepth(time_block->GetParent(), ++depth);
        }

        return depth;
    }
}
