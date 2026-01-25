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

#pragma once

//= INCLUDES ==========
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>
#include <fstream>
#include <chrono>
#include "Definitions.h"
//=====================

namespace spartan
{
    class Breadcrumbs
    {
    public:
        static constexpr uint32_t max_markers          = 256;
        static constexpr uint32_t max_marker_name_size = 128;
        static constexpr uint32_t max_history_frames   = 3;

        enum class MarkerState : uint8_t
        {
            Empty,     // slot is unused
            Started,   // marker began but hasn't ended
            Completed  // marker completed successfully
        };

        struct Marker
        {
            std::array<char, max_marker_name_size> name = {};
            MarkerState state                           = MarkerState::Empty;
            uint64_t frame_index                        = 0;
            uint32_t depth                              = 0;
            std::chrono::steady_clock::time_point start_time;
        };

        static void Initialize()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_markers.resize(max_markers);
            m_frame_index   = 0;
            m_current_index = 0;
            m_current_depth = 0;
            m_initialized   = true;
        }

        static void Shutdown()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_markers.clear();
            m_initialized = false;
        }

        static void StartFrame()
        {
            if (!m_initialized)
                return;

            std::lock_guard<std::mutex> lock(m_mutex);

            // increment frame index
            m_frame_index++;

            // clear old completed markers (keep history for a few frames)
            for (auto& marker : m_markers)
            {
                if (marker.state == MarkerState::Completed && (m_frame_index - marker.frame_index) > max_history_frames)
                {
                    marker.state = MarkerState::Empty;
                }
            }
        }

        static void BeginMarker(const char* name)
        {
            if (!m_initialized || !name)
                return;

            std::lock_guard<std::mutex> lock(m_mutex);

            // find an empty slot or wrap around
            uint32_t start_index = m_current_index;
            while (m_markers[m_current_index].state == MarkerState::Started)
            {
                m_current_index = (m_current_index + 1) % max_markers;
                if (m_current_index == start_index)
                {
                    // buffer full of started markers, force evict oldest
                    break;
                }
            }

            Marker& marker = m_markers[m_current_index];
            marker.state   = MarkerState::Started;

            // copy name safely
            size_t name_len = strlen(name);
            if (name_len >= max_marker_name_size)
            {
                name_len = max_marker_name_size - 1;
            }
            memcpy(marker.name.data(), name, name_len);
            marker.name[name_len] = '\0';

            marker.frame_index = m_frame_index;
            marker.depth       = m_current_depth;
            marker.start_time  = std::chrono::steady_clock::now();

            m_current_depth++;
            m_current_index = (m_current_index + 1) % max_markers;
        }

        static void EndMarker()
        {
            if (!m_initialized)
                return;

            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_current_depth > 0)
            {
                m_current_depth--;
            }

            // find the most recent started marker at current depth
            for (int32_t i = static_cast<int32_t>(m_markers.size()) - 1; i >= 0; i--)
            {
                uint32_t index = (m_current_index + max_markers - 1 - (static_cast<uint32_t>(m_markers.size()) - 1 - i)) % max_markers;
                Marker& marker = m_markers[index];
                if (marker.state == MarkerState::Started && marker.depth == m_current_depth)
                {
                    marker.state = MarkerState::Completed;
                    return;
                }
            }
        }

        static void OnDeviceLost()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            WriteReport();
        }

    private:
        static void WriteReport()
        {
            std::string report;
            report.reserve(4096);

            report += "=================================================\n";
            report += "GPU CRASH REPORT - Breadcrumbs\n";
            report += "=================================================\n\n";

            // collect incomplete markers
            std::vector<const Marker*> incomplete_markers;
            for (const auto& marker : m_markers)
            {
                if (marker.state == MarkerState::Started)
                {
                    incomplete_markers.push_back(&marker);
                }
            }

            if (incomplete_markers.empty())
            {
                report += "No incomplete markers found.\n";
                report += "The GPU crash may have occurred outside of tracked markers.\n";
            }
            else
            {
                report += "INCOMPLETE MARKERS (started but never completed):\n";
                report += "-------------------------------------------------\n\n";

                // sort by frame index and depth
                std::sort(incomplete_markers.begin(), incomplete_markers.end(),
                    [](const Marker* a, const Marker* b)
                    {
                        if (a->frame_index != b->frame_index)
                            return a->frame_index < b->frame_index;
                        return a->depth < b->depth;
                    });

                for (const auto* marker : incomplete_markers)
                {
                    auto now  = std::chrono::steady_clock::now();
                    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - marker->start_time);

                    // indent based on depth
                    for (uint32_t d = 0; d < marker->depth; d++)
                    {
                        report += "  ";
                    }

                    report += "-> Frame " + std::to_string(marker->frame_index);
                    report += " | " + std::string(marker->name.data());
                    report += " | Running for: " + std::to_string(diff.count()) + "ms\n";
                }
            }

            report += "\n=================================================\n";
            report += "This report indicates which GPU operations were in\n";
            report += "progress when the device was lost. The last marker\n";
            report += "without a matching end is likely the culprit.\n";
            report += "=================================================\n";

            // write to file
            std::ofstream file("gpu_crash.txt", std::ios::binary);
            if (file.good())
            {
                file.write(report.c_str(), report.size());
                file.close();
            }
        }

        inline static std::vector<Marker> m_markers;
        inline static std::mutex m_mutex;
        inline static uint64_t m_frame_index   = 0;
        inline static uint32_t m_current_index = 0;
        inline static uint32_t m_current_depth = 0;
        inline static bool m_initialized       = false;
    };
}
