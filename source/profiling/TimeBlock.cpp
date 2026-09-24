/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ======================
#include "pch.h"
#include "TimeBlock.h"
#include "Profiler.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Device.h"
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

    void TimeBlock::Begin(
        const uint32_t id,
        const char* name,
        const TimeBlockType type,
        const uint32_t parent_id,
        const uint32_t parent_tree_depth,
        RHI_CommandList* cmd_list,
        const RHI_Queue_Type queue_type
    )
    {
        m_id                    = id;
        m_name                  = name;
        m_parent_id             = parent_id;
        m_type                  = type;
        m_queue_type            = queue_type;
        m_duration              = 0.0f;
        m_is_complete           = false;
        m_cmd_list              = cmd_list;
        m_timestamp_sample.reset();
        m_timestamp_index_start = 0;
        m_timestamp_index_end   = 0;
        m_start_ms              = 0.0f;
        m_end_ms                = 0.0f;
        m_tree_depth            =
            parent_id != 0 ?
                parent_tree_depth + 1 :
                0;
        m_max_tree_depth        = max(m_max_tree_depth, m_tree_depth);

        // record cpu time for timeline position
        m_start    = RHI_Device::GetCpuTimestampMs();
        m_start_ms = Profiler::GetCpuOffsetMs(m_start);
        m_frame_start_ms = Profiler::GetFrameStartMs();

        if (type == TimeBlockType::Gpu)
        {
            m_calibration = Profiler::GetGpuCalibration(queue_type);
            m_timestamp_sample = cmd_list->GetTimestampSample();
            m_timestamp_index_start = cmd_list->begin_timestamp();
        }
    }

    void TimeBlock::End()
    {
        if (m_type == TimeBlockType::Cpu)
        {
            m_end = RHI_Device::GetCpuTimestampMs();
        }
        else if (m_type == TimeBlockType::Gpu)
        {
            m_timestamp_index_end = m_cmd_list->end_timestamp();
        }

        // compute duration and timeline offsets
        if (m_type == TimeBlockType::Cpu)
        {
            m_duration = static_cast<float>(m_end - m_start);
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

    bool TimeBlock::TryResolveGpu()
    {
        if (!m_timestamp_sample) return true;
        if (!m_timestamp_sample->ready && m_cmd_list)
            m_cmd_list->ReadbackTimestampsForProfiler();
        return m_timestamp_sample->ready;
    }

    uint64_t TimeBlock::GetTimestampRawTick(uint32_t index) const
    {
        return m_timestamp_sample && m_timestamp_sample->ready && index < m_timestamp_sample->ticks.size()
            ? m_timestamp_sample->ticks[index] : 0;
    }

    void TimeBlock::ResolveGpuTimestamps(uint64_t global_reference_tick, float timestamp_period, uint64_t end_tick_override /*= 0*/)
    {
        if (m_type != TimeBlockType::Gpu || !m_cmd_list)
        {
            return;
        }

        m_gpu_timing_valid = false;
        if (!m_timestamp_sample || !m_timestamp_sample->ready ||
            m_timestamp_index_start >= m_timestamp_sample->count ||
            m_timestamp_index_end >= m_timestamp_sample->count ||
            !m_timestamp_sample->available[m_timestamp_index_start] ||
            !m_timestamp_sample->available[m_timestamp_index_end] || !m_calibration.valid_bits)
        {
            m_start_ms = m_end_ms = m_duration = 0.0f;
            return;
        }
        const uint64_t mask = m_calibration.valid_bits == 64 ? UINT64_MAX : (uint64_t(1) << m_calibration.valid_bits) - 1;
        const uint64_t start_tick = GetTimestampRawTick(m_timestamp_index_start) & mask;
        const uint64_t end_tick = (end_tick_override ? end_tick_override : GetTimestampRawTick(m_timestamp_index_end)) & mask;
        const uint64_t elapsed = (end_tick - start_tick) & mask;
        const double period = m_calibration.period_ns > 0.0 ? m_calibration.period_ns : timestamp_period;
        if (elapsed > (mask >> 1) || period <= 0.0) return;
        m_duration = static_cast<float>(static_cast<double>(elapsed) * period * 1e-6);
        const uint64_t reference = m_calibration.calibrated ? m_calibration.gpu_tick : global_reference_tick;
        const uint64_t delta = (start_tick - reference) & mask;
        const double signed_delta = delta > (mask >> 1) ? -static_cast<double>((reference - start_tick) & mask) : static_cast<double>(delta);
        const double origin_ms = m_calibration.calibrated ? m_calibration.cpu_ms - m_frame_start_ms : 0.0;
        m_start_ms = static_cast<float>(origin_ms + signed_delta * period * 1e-6);
        m_end_ms = m_start_ms + m_duration;
        m_gpu_timing_valid = true;
    }

    void TimeBlock::ResolveGpuDuration(uint64_t end_tick_override /*= 0*/)
    {
        if (m_type != TimeBlockType::Gpu || !m_cmd_list)
        {
            return;
        }

        uint64_t start_tick = GetTimestampRawTick(m_timestamp_index_start);
        uint64_t end_tick   = end_tick_override != 0 ? end_tick_override : GetTimestampRawTick(m_timestamp_index_end);
        if (end_tick > start_tick)
        {
            uint64_t duration_ticks = end_tick - start_tick;
            m_duration = clamp(static_cast<float>(duration_ticks * RHI_Device::PropertyGetTimestampPeriod() * 1e-6f), 0.0f, 1000.0f);
        }
        else
        {
            m_duration = 0.0f;
        }

        m_end_ms   = m_start_ms + m_duration;
    }
}
