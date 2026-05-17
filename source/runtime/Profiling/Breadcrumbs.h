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

//= INCLUDES ======================
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>
#include <chrono>
#include <algorithm>
#include "Definitions.h"
#include "../RHI/RHI_Definitions.h"
//=================================

namespace spartan
{
    class RHI_Buffer;
    class RHI_CommandList;

    class Breadcrumbs
    {
    public:
        static constexpr uint32_t max_markers          = 256;
        static constexpr uint32_t max_marker_name_size = 128;
        static constexpr uint32_t max_history_frames   = 3;
        static constexpr uint32_t max_gpu_markers      = 512;
        static constexpr uint32_t gpu_marker_completed = 0xFFFFFFFF;

        // per-queue dedicated buffer count, each queue type owns its own VkBuffer so the vulkan
        // sync validator never sees two queues writing the same resource without synchronization
        // which is what triggers WRITE_RACING_WRITE even when the byte ranges do not overlap
        static constexpr uint32_t queue_count = 3;

        enum class MarkerState : uint8_t
        {
            Empty,    // slot is unused
            Started,  // marker began but hasn't ended
            Completed // marker completed successfully
        };

        struct Marker
        {
            std::array<char, max_marker_name_size> name = {};
            MarkerState state                           = MarkerState::Empty;
            uint64_t frame_index                        = 0;
            uint32_t depth                              = 0;
            std::chrono::steady_clock::time_point start_time;
        };

        static void Initialize();
        static void Shutdown();

        static void StartFrame()
        {
            if (!m_initialized)
            {
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            m_frame_index++;

            // clear old completed cpu markers
            for (auto& marker : m_markers)
            {
                if (marker.state == MarkerState::Completed && (m_frame_index - marker.frame_index) > max_history_frames)
                {
                    marker.state = MarkerState::Empty;
                }
            }

            // reset gpu markers for the new frame
            ResetGpuMarkers();
        }

        static void BeginMarker(const char* name)
        {
            if (!m_initialized || !name)
            {
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            // find an empty slot or wrap around
            uint32_t start_index = m_current_index;
            while (m_markers[m_current_index].state == MarkerState::Started)
            {
                m_current_index = (m_current_index + 1) % max_markers;
                if (m_current_index == start_index)
                {
                    break;
                }
            }

            Marker& marker = m_markers[m_current_index];
            marker.state   = MarkerState::Started;

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
            {
                return;
            }

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

        // gpu-side breadcrumbs - allocates a slot in the queue's dedicated buffer and records the
        // name, returns -1 if full, the returned slot is local to the queue's own buffer
        static int32_t GpuMarkerBegin(const char* name, RHI_Queue_Type queue_type);
        static void GpuMarkerEnd(int32_t marker_index);

        // returns the dedicated breadcrumb buffer for the given queue type, may return nullptr
        // if breadcrumbs were not initialized or the queue type is out of range
        static RHI_Buffer* GetGpuBuffer(RHI_Queue_Type queue_type);

        static void OnDeviceLost()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            WriteReport();
        }

    private:
        static void ResetGpuMarkers();
        static void WriteReport();

        // cpu-side markers
        inline static std::vector<Marker> m_markers;
        inline static std::mutex m_mutex;
        inline static uint64_t m_frame_index   = 0;
        inline static uint32_t m_current_index = 0;
        inline static uint32_t m_current_depth = 0;
        inline static bool m_initialized       = false;

        // gpu-side markers, one buffer plus one name array plus one counter per queue type so
        // graphics, compute and copy submissions never touch the same vkbuffer
        inline static std::array<RHI_Buffer*, queue_count> m_gpu_buffers          = { nullptr, nullptr, nullptr };
        inline static std::array<uint32_t, queue_count> m_gpu_marker_counts       = { 0, 0, 0 };
        inline static std::array<std::array<const char*, max_gpu_markers>, queue_count> m_gpu_marker_names = {};
        inline static std::array<int32_t, max_gpu_markers> m_gpu_marker_begin_to_slot                      = {};
    };
}
