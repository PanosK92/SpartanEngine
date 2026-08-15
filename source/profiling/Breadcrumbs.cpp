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

//= INCLUDES ==================
#include "pch.h"
#include "Breadcrumbs.h"
#include "../rhi/RHI_Buffer.h"
#include <sstream>
//=============================

namespace spartan
{
    namespace
    {
        // maps a queue type to a fixed index into the per-queue arrays, returns -1 for unsupported types
        int32_t queue_index_from_type(RHI_Queue_Type queue_type)
        {
            switch (queue_type)
            {
                case RHI_Queue_Type::Graphics: return 0;
                case RHI_Queue_Type::Compute:  return 1;
                case RHI_Queue_Type::Copy:     return 2;
                case RHI_Queue_Type::Present:  return -1; // present does not record gpu breadcrumbs
                default:                       return -1;
            }
        }

        const char* queue_name_from_type(RHI_Queue_Type queue_type)
        {
            switch (queue_type)
            {
                case RHI_Queue_Type::Graphics: return "breadcrumb_gpu_graphics";
                case RHI_Queue_Type::Compute:  return "breadcrumb_gpu_compute";
                case RHI_Queue_Type::Copy:     return "breadcrumb_gpu_copy";
                default:                       return "breadcrumb_gpu_unknown";
            }
        }
    }

    void Breadcrumbs::Initialize()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // cpu markers
        m_markers.resize(max_markers);
        m_frame_index   = 0;
        m_current_index = 0;
        m_current_depth = 0;

        // gpu breadcrumb buffers, one per queue type, host visible and coherent so the cpu can
        // read them after a crash, separate vkbuffer per queue removes any cross-queue race
        const RHI_Queue_Type queue_types[queue_count] = { RHI_Queue_Type::Graphics, RHI_Queue_Type::Compute, RHI_Queue_Type::Copy };
        for (uint32_t i = 0; i < queue_count; i++)
        {
            m_gpu_buffers[i] = new RHI_Buffer(
                RHI_Buffer_Type::Storage,
                sizeof(uint32_t),
                max_gpu_markers,
                nullptr,
                true,
                queue_name_from_type(queue_types[i])
            );

            m_gpu_marker_counts[i] = 0;
            m_gpu_marker_names[i].fill(nullptr);
        }

        m_gpu_marker_begin_to_slot.fill(-1);

        m_initialized = true;
    }

    void Breadcrumbs::Shutdown()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_markers.clear();

        for (uint32_t i = 0; i < queue_count; i++)
        {
            if (m_gpu_buffers[i])
            {
                delete m_gpu_buffers[i];
                m_gpu_buffers[i] = nullptr;
            }
            m_gpu_marker_counts[i] = 0;
        }

        m_initialized = false;
    }

    int32_t Breadcrumbs::GpuMarkerBegin(const char* name, RHI_Queue_Type queue_type)
    {
        if (!m_initialized || !name)
        {
            return -1;
        }

        int32_t qi = queue_index_from_type(queue_type);
        if (qi < 0 || !m_gpu_buffers[qi])
        {
            return -1;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_gpu_marker_counts[qi] >= max_gpu_markers)
        {
            return -1;
        }

        uint32_t slot                = m_gpu_marker_counts[qi]++;
        m_gpu_marker_names[qi][slot] = name;

        return static_cast<int32_t>(slot);
    }

    RHI_Buffer* Breadcrumbs::GetGpuBuffer(RHI_Queue_Type queue_type)
    {
        int32_t qi = queue_index_from_type(queue_type);
        if (qi < 0)
        {
            return nullptr;
        }

        return m_gpu_buffers[qi];
    }

    void Breadcrumbs::GpuMarkerEnd(int32_t marker_index)
    {
        if (!m_initialized || marker_index < 0 || marker_index >= static_cast<int32_t>(max_gpu_markers))
        {
            return;
        }

        // nothing to track on the cpu side for end - the gpu buffer write handles it
    }

    void Breadcrumbs::ResetGpuMarkers()
    {
        m_gpu_marker_begin_to_slot.fill(-1);

        for (uint32_t qi = 0; qi < queue_count; qi++)
        {
            if (!m_gpu_buffers[qi])
            {
                continue;
            }

            void* mapped = m_gpu_buffers[qi]->GetMappedData();

            // keep the frame that is about to be wiped, a fault raised by these commands only
            // surfaces on the next submit and the report would otherwise come out empty
            if (m_gpu_marker_counts[qi] > 0)
            {
                m_gpu_marker_names_prev[qi] = m_gpu_marker_names[qi];
                if (mapped)
                {
                    memcpy(m_gpu_marker_values_prev[qi].data(), mapped, max_gpu_markers * sizeof(uint32_t));
                }
                else
                {
                    m_gpu_marker_values_prev[qi].fill(0);
                }

                m_has_prev_frame = true;
            }

            m_gpu_marker_counts[qi] = 0;
            m_gpu_marker_names[qi].fill(nullptr);

            // zero out the mapped buffer so all slots read as "not reached"
            if (mapped)
            {
                memset(mapped, 0, max_gpu_markers * sizeof(uint32_t));
            }
        }
    }

    void Breadcrumbs::WriteReport()
    {
        std::string report;
        report.reserve(4096);

        // collect incomplete cpu markers (the crash path)
        std::vector<const Marker*> incomplete_markers;
        for (const auto& marker : m_markers)
        {
            if (marker.state == MarkerState::Started)
            {
                incomplete_markers.push_back(&marker);
            }
        }

        std::sort(incomplete_markers.begin(), incomplete_markers.end(),
            [](const Marker* a, const Marker* b)
            {
                if (a->frame_index != b->frame_index)
                {
                    return a->frame_index < b->frame_index;
                }
                return a->depth < b->depth;
            });

        // collect gpu marker states across all per-queue buffers
        std::string gpu_crash_marker_name;
        std::string last_completed_name;
        std::string first_incomplete_name;
        bool has_any_gpu_marker = false;

        const char* queue_label[queue_count] = { "graphics", "compute", "copy" };

        // one line per queue, emitted before the full dump so the shape of the failure survives even
        // if the process dies partway through writing
        struct QueueSummary
        {
            std::string last_completed;
            std::string crashed;
            std::string first_incomplete;
            uint32_t completed_count  = 0;
            uint32_t incomplete_count = 0;
        };
        std::array<QueueSummary, queue_count> summary = {};

        auto collect_queue = [&](uint32_t qi, const char* const* names, const uint32_t* values)
        {
            for (uint32_t i = 0; i < max_gpu_markers; i++)
            {
                if (!names[i] || values[i] != gpu_marker_completed)
                {
                    continue;
                }

                report += "  [completed]   [" + std::string(queue_label[qi]) + "] " + std::string(names[i]) + "\n";
                last_completed_name = names[i];
                has_any_gpu_marker  = true;

                summary[qi].last_completed = names[i];
                summary[qi].completed_count++;
            }

            for (uint32_t i = 0; i < max_gpu_markers; i++)
            {
                if (!names[i] || values[i] == 0 || values[i] == gpu_marker_completed)
                {
                    continue;
                }

                report += "  [gpu crash]   [" + std::string(queue_label[qi]) + "] " + std::string(names[i]) + "\n";
                gpu_crash_marker_name = names[i];
                has_any_gpu_marker    = true;

                summary[qi].crashed = names[i];
            }

            for (uint32_t i = 0; i < max_gpu_markers; i++)
            {
                if (!names[i] || values[i] != 0)
                {
                    continue;
                }

                report += "  [incomplete]  [" + std::string(queue_label[qi]) + "] " + std::string(names[i]) + "\n";
                has_any_gpu_marker = true;

                if (first_incomplete_name.empty())
                {
                    first_incomplete_name = names[i];
                }

                if (summary[qi].first_incomplete.empty())
                {
                    summary[qi].first_incomplete = names[i];
                }
                summary[qi].incomplete_count++;
            }
        };

        for (uint32_t qi = 0; qi < queue_count; qi++)
        {
            const uint32_t* gpu_data = m_gpu_buffers[qi] ? static_cast<const uint32_t*>(m_gpu_buffers[qi]->GetMappedData()) : nullptr;
            if (!gpu_data)
            {
                continue;
            }

            collect_queue(qi, m_gpu_marker_names[qi].data(), gpu_data);
        }

        // the faulting commands usually belong to the frame that was already reset, fall back to
        // the snapshot so a device loss reported on the next submit still names a pass
        if (!has_any_gpu_marker && m_has_prev_frame)
        {
            report += "  (current frame recorded nothing, showing the previous frame)\n";
            for (uint32_t qi = 0; qi < queue_count; qi++)
            {
                collect_queue(qi, m_gpu_marker_names_prev[qi].data(), m_gpu_marker_values_prev[qi].data());
            }
        }

        // cpu incomplete markers (the crash call stack)
        uint32_t deepest_depth = 0;
        for (const auto* m : incomplete_markers)
            deepest_depth = std::max(deepest_depth, m->depth);

        for (const auto* marker : incomplete_markers)
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - marker->start_time);

            bool is_crash_point = (marker->depth == deepest_depth);

            for (uint32_t d = 0; d < marker->depth; d++)
                report += "  ";

            report += is_crash_point ? "  [crash]       " : "  [in progress] ";
            report += std::string(marker->name.data());
            report += " | frame " + std::to_string(marker->frame_index);
            report += " | " + std::to_string(elapsed.count()) + "ms\n";
        }

        // deduce crash point
        std::string verdict;
        if (!gpu_crash_marker_name.empty())
        {
            verdict = "crash point: " + gpu_crash_marker_name + " (gpu stopped executing here)";
        }
        else if (!last_completed_name.empty() && !first_incomplete_name.empty())
        {
            verdict = "crash point: between " + last_completed_name + " (completed) and " + first_incomplete_name + " (incomplete)";
        }
        else if (!incomplete_markers.empty())
        {
            verdict = "crash point: " + std::string(incomplete_markers.back()->name.data());
        }
        else if (!has_any_gpu_marker)
        {
            verdict = "no markers were reached, the crash occurred before any tracked operation.";
        }
        else
        {
            verdict = "all tracked passes completed, the crash occurred after the last tracked operation.";
        }

        // the process is usually killed while this is still being written, so the verdict goes out
        // first and every line is logged on its own, a truncated flush then only costs the tail
        Log::SetLogToFile(true);
        SP_LOG_ERROR("========================= GPU CRASH REPORT =========================");
        SP_LOG_ERROR("%s", verdict.c_str());

        for (uint32_t qi = 0; qi < queue_count; qi++)
        {
            const QueueSummary& s = summary[qi];
            if (s.completed_count == 0 && s.incomplete_count == 0 && s.crashed.empty())
            {
                continue;
            }

            SP_LOG_ERROR("  queue %s: %u completed, %u incomplete | last completed: %s | crashed in: %s | first incomplete: %s",
                queue_label[qi],
                s.completed_count,
                s.incomplete_count,
                s.last_completed.empty()   ? "none" : s.last_completed.c_str(),
                s.crashed.empty()          ? "none" : s.crashed.c_str(),
                s.first_incomplete.empty() ? "none" : s.first_incomplete.c_str());
        }

        std::istringstream stream(report);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty())
            {
                SP_LOG_ERROR("%s", line.c_str());
            }
        }

        SP_LOG_ERROR("=====================================================================");
    }
}
