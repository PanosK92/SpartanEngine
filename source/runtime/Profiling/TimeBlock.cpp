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

//= INCLUDES ======================
#include "pch.h"
#include "TimeBlock.h"
#include "Profiler.h"
#include "../RHI/RHI_CommandList.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    uint32_t TimeBlock::m_max_tree_depth = 0;

    TimeBlock::~TimeBlock()
    {

    }

    void TimeBlock::Begin(const uint32_t id, const char* name, TimeBlockType type, const TimeBlock* parent /*= nullptr*/, RHI_CommandList* cmd_list /*= nullptr*/, RHI_Queue_Type queue_type /*= RHI_Queue_Type::Max*/)
    {
        m_id             = id;
        m_name           = name;
        m_parent         = parent;
        m_tree_depth     = FindTreeDepth(this);
        m_type           = type;
        m_queue_type     = queue_type;
        m_max_tree_depth = max(m_max_tree_depth, m_tree_depth);

        if (cmd_list)
        {
            m_cmd_list = cmd_list;
        }

        // record cpu time for timeline position
        m_start    = chrono::high_resolution_clock::now();
        m_start_ms = Profiler::GetCpuOffsetMs(m_start);

        if (type == TimeBlockType::Gpu)
        {
            m_timestamp_index = cmd_list->BeginTimestamp();
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
            m_cmd_list->EndTimestamp();
        }

        // compute duration and timeline offsets
        if (m_type == TimeBlockType::Cpu)
        {
            const chrono::duration<double, milli> ms = m_end - m_start;
            m_duration = static_cast<float>(ms.count());
            m_end_ms   = m_start_ms + m_duration;
        }
        else if (m_type == TimeBlockType::Gpu)
        {
            // gpu duration and position will be resolved later with fresh data in ReadTimeBlocks(),
            // for now just use cpu time as a placeholder so the block is considered complete
            m_end_ms = m_start_ms;
        }

        m_is_complete = true;
    }

    void TimeBlock::ResolveGpuTimestamps(uint64_t global_reference_tick, float timestamp_period)
    {
        if (m_type != TimeBlockType::Gpu || !m_cmd_list)
            return;

        // recompute duration from fresh (post-execution) timestamp data
        m_duration = m_cmd_list->GetTimestampResult(m_timestamp_index);

        // compute position relative to the global frame reference
        uint64_t start_tick = m_cmd_list->GetTimestampRawTick(m_timestamp_index);
        if (start_tick >= global_reference_tick && global_reference_tick != 0)
        {
            m_start_ms = static_cast<float>((start_tick - global_reference_tick) * timestamp_period * 1e-6f);
        }

        m_end_ms = m_start_ms + m_duration;
    }

    void TimeBlock::ResolveGpuDuration()
    {
        if (m_type != TimeBlockType::Gpu || !m_cmd_list)
            return;

        // approximate duration from existing (possibly stale) query pool data, no gpu wait needed
        m_duration = m_cmd_list->GetTimestampResult(m_timestamp_index);
        m_end_ms   = m_start_ms + m_duration;
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
