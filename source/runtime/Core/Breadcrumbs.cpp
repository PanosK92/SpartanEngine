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
#include "../RHI/RHI_Buffer.h"
//=============================

namespace spartan
{
    void Breadcrumbs::Initialize()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // cpu markers
        m_markers.resize(max_markers);
        m_frame_index   = 0;
        m_current_index = 0;
        m_current_depth = 0;

        // gpu breadcrumb buffer - host visible and host coherent so the cpu can read it after a crash
        m_gpu_buffer = new RHI_Buffer(
            RHI_Buffer_Type::Storage,
            sizeof(uint32_t),
            max_gpu_markers,
            nullptr,
            true,
            "breadcrumb_gpu"
        );

        m_gpu_marker_count = 0;
        m_gpu_marker_names.fill(nullptr);
        m_gpu_marker_begin_to_slot.fill(-1);

        m_initialized = true;
    }

    void Breadcrumbs::Shutdown()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_markers.clear();

        if (m_gpu_buffer)
        {
            delete m_gpu_buffer;
            m_gpu_buffer = nullptr;
        }

        m_gpu_marker_count = 0;
        m_initialized      = false;
    }

    int32_t Breadcrumbs::GpuMarkerBegin(const char* name)
    {
        if (!m_initialized || !m_gpu_buffer || !name)
            return -1;

        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_gpu_marker_count >= max_gpu_markers)
            return -1;

        uint32_t slot            = m_gpu_marker_count++;
        m_gpu_marker_names[slot] = name;

        return static_cast<int32_t>(slot);
    }

    void Breadcrumbs::GpuMarkerEnd(int32_t marker_index)
    {
        if (!m_initialized || marker_index < 0 || marker_index >= static_cast<int32_t>(max_gpu_markers))
            return;

        // nothing to track on the cpu side for end - the gpu buffer write handles it
    }

    void Breadcrumbs::ResetGpuMarkers()
    {
        if (!m_gpu_buffer)
            return;

        m_gpu_marker_count = 0;
        m_gpu_marker_names.fill(nullptr);
        m_gpu_marker_begin_to_slot.fill(-1);

        // zero out the mapped buffer so all slots read as "not reached"
        void* mapped = m_gpu_buffer->GetMappedData();
        if (mapped)
        {
            memset(mapped, 0, max_gpu_markers * sizeof(uint32_t));
        }
    }

    void Breadcrumbs::WriteReport()
    {
        std::string report;
        report.reserve(4096);

        report += "========================= GPU CRASH REPORT =========================\n\n";

        // collect incomplete cpu markers (the crash path)
        std::vector<const Marker*> incomplete_markers;
        for (const auto& marker : m_markers)
        {
            if (marker.state == MarkerState::Started)
                incomplete_markers.push_back(&marker);
        }

        std::sort(incomplete_markers.begin(), incomplete_markers.end(),
            [](const Marker* a, const Marker* b)
            {
                if (a->frame_index != b->frame_index)
                    return a->frame_index < b->frame_index;
                return a->depth < b->depth;
            });

        // collect gpu marker states
        uint32_t* gpu_data                 = m_gpu_buffer ? static_cast<uint32_t*>(m_gpu_buffer->GetMappedData()) : nullptr;
        std::string gpu_crash_marker_name;
        std::string last_completed_name;
        std::string first_incomplete_name;
        bool has_any_gpu_marker            = false;

        if (gpu_data)
        {
            // gpu completed markers (these finished before the crash)
            for (uint32_t i = 0; i < m_gpu_marker_count; i++)
            {
                if (!m_gpu_marker_names[i])
                    continue;

                if (gpu_data[i] == gpu_marker_completed)
                {
                    report += "  [completed]   " + std::string(m_gpu_marker_names[i]) + "\n";
                    last_completed_name = m_gpu_marker_names[i];
                    has_any_gpu_marker  = true;
                }
            }

            // gpu in-progress markers (where the gpu actually stopped mid-pass)
            for (uint32_t i = 0; i < m_gpu_marker_count; i++)
            {
                if (!m_gpu_marker_names[i] || gpu_data[i] == 0 || gpu_data[i] == gpu_marker_completed)
                    continue;

                report += "  [gpu crash]   " + std::string(m_gpu_marker_names[i]) + "\n";
                gpu_crash_marker_name = m_gpu_marker_names[i];
                has_any_gpu_marker    = true;
            }

            // gpu not-reached markers (registered but the gpu never got to them)
            for (uint32_t i = 0; i < m_gpu_marker_count; i++)
            {
                if (!m_gpu_marker_names[i] || gpu_data[i] != 0)
                    continue;

                report += "  [incomplete]  " + std::string(m_gpu_marker_names[i]) + "\n";
                has_any_gpu_marker = true;

                if (first_incomplete_name.empty())
                {
                    first_incomplete_name = m_gpu_marker_names[i];
                }
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
        report += "\n---------------------------------------------------------------------\n";
        if (!gpu_crash_marker_name.empty())
        {
            report += "crash point: " + gpu_crash_marker_name + " (gpu stopped executing here)\n";
        }
        else if (!last_completed_name.empty() && !first_incomplete_name.empty())
        {
            report += "crash point: between " + last_completed_name + " (completed) and " + first_incomplete_name + " (incomplete)\n";
        }
        else if (!incomplete_markers.empty())
        {
            report += "crash point: " + std::string(incomplete_markers.back()->name.data()) + "\n";
        }
        else if (!has_any_gpu_marker)
        {
            report += "no markers were reached, the crash occurred before any tracked operation.\n";
        }
        else
        {
            report += "all tracked passes completed, the crash occurred after the last tracked operation.\n";
        }
        report += "=====================================================================\n";

        Log::SetLogToFile(true);
        SP_LOG_ERROR("%s", report.c_str());
    }
}
