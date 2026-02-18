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

//= INCLUDES ========================
#include "pch.h"
#include "Profiler.h"
#include "../ImGui/ImGui_Extension.h"
#include "Profiling/Profiler.h"
#include "../RHI/RHI_Device.h"
#include "../Memory/Allocator.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace
{
    ImU32 get_time_block_color(const char* name, bool is_compute = false)
    {
        if (is_compute)
        {
            // red hue range for compute blocks, varied by name hash for distinction
            size_t hash_value = hash<string>{}(name);
            float hue = 0.0f + static_cast<float>(hash_value % 30) / 360.0f; // 0-30 degrees (red range)
            ImVec4 color = ImColor::HSV(hue, 0.7f, 0.8f);
            return IM_COL32(
                static_cast<int>(color.x * 255),
                static_cast<int>(color.y * 255),
                static_cast<int>(color.z * 255),
                255
            );
        }

        size_t hash_value = hash<string>{}(name);
        float hue = static_cast<float>(hash_value % 360) / 360.0f;
        ImVec4 color = ImColor::HSV(hue, 0.55f, 0.75f);
        return IM_COL32(
            static_cast<int>(color.x * 255),
            static_cast<int>(color.y * 255),
            static_cast<int>(color.z * 255),
            255
        );
    }

    void show_time_block(const spartan::TimeBlock& time_block)
    {
        const float m_tree_depth_stride = 10;

        const char* name        = time_block.GetName();
        const float duration    = time_block.GetDuration();
        const float fraction    = duration / 10.0f;
        const float width       = fraction * ImGui::GetContentRegionAvail().x;
        const ImVec2 pos_screen = ImGui::GetCursorScreenPos();
        const ImVec2 pos        = ImGui::GetCursorPos();
        const float text_height = ImGui::CalcTextSize(name, nullptr, true).y;

        ImU32 col = get_time_block_color(name);

        ImGui::GetWindowDrawList()->AddRectFilled(pos_screen, ImVec2(pos_screen.x + width, pos_screen.y + text_height), col);

        ImGui::SetCursorPos(ImVec2(pos.x + m_tree_depth_stride * time_block.GetTreeDepth(), pos.y));
        ImGui::Text("%s - %.2f ms", name, duration);
    }

    void show_memory_bar(const char* label, float used_mb, float budget_mb, float total_mb, ImVec2 size = ImVec2(-1, 0))
    {
        ImVec2 pos  = ImGui::GetCursorScreenPos();
        float fullW = (size.x <= 0.0f ? ImGui::GetContentRegionAvail().x : size.x);
        float fullH = (size.y <= 0.0f ? ImGui::GetTextLineHeightWithSpacing() : size.y);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        ImU32 col_total  = IM_COL32(20, 30, 60, 255);
        ImU32 col_budget = IM_COL32(80, 150, 220, 255);
        float usedFrac = (budget_mb > 0.0f) ? (used_mb / budget_mb) : 0.0f;
        usedFrac = ImClamp(usedFrac, 0.0f, 1.0f);
        ImU32 col_used;
        if (usedFrac < 0.5f)
        {
            float t = usedFrac / 0.5f;
            col_used = IM_COL32(
                (int)(80  + t * (220-80)),
                (int)(220 - t * (220-180)),
                80, 255
            );
        }
        else
        {
            float t = (usedFrac - 0.5f) / 0.5f;
            col_used = IM_COL32(220, (int)(180 - t * 180), 80, 255);
        }

        draw_list->AddRectFilled(pos, ImVec2(pos.x + fullW, pos.y + fullH), col_total);

        float budgetFrac = (budget_mb > 0.0f && total_mb > 0.0f) ? (budget_mb / total_mb) : 0.0f;
        draw_list->AddRectFilled(pos, ImVec2(pos.x + fullW * budgetFrac, pos.y + fullH), col_budget);
        draw_list->AddRectFilled(pos, ImVec2(pos.x + fullW * usedFrac * budgetFrac, pos.y + fullH), col_used);
        draw_list->AddRect(pos, ImVec2(pos.x + fullW, pos.y + fullH), IM_COL32(255, 255, 255, 255));

        char buf[128];
        snprintf(buf, sizeof(buf), "%s %.0f/%.0f MB (Budget %.0f MB)", label, used_mb, total_mb, budget_mb);
        ImGui::RenderTextClipped(pos, ImVec2(pos.x + fullW, pos.y + fullH), buf, nullptr, nullptr, ImVec2(0.5f, 0.5f));

        ImGui::Dummy(ImVec2(fullW, fullH));
    }

    int mode_hardware = 0; // 0: gpu, 1: cpu
    int mode_sort     = 1; // 0: alphabetically, 1: by duration
    int mode_view     = 1; // 0: list, 1: timeline
}

Profiler::Profiler(Editor* editor) : Widget(editor)
{
    m_flags        |= ImGuiWindowFlags_NoScrollbar;
    m_title        = "Profiler";
    m_visible      = false;
    m_size_initial = Vector2(1000, 715);
    m_size_min     = Vector2(600, 500);
    m_plot.fill(16.0f);
}

void Profiler::OnTick()
{
    // let the runtime profiler know if the widget is open so it can skip the gpu stall when nobody is watching
    spartan::Profiler::SetVisualized(m_visible);
}

void Profiler::OnTickVisible()
{
    int previous_item_type = mode_hardware;

    // detect mode changes and trigger auto-fit
    if (mode_hardware != m_prev_mode_hardware || mode_view != m_prev_mode_view)
    {
        m_timeline_needs_fit  = true;
        m_prev_mode_hardware  = mode_hardware;
        m_prev_mode_view      = mode_view;
    }

    // controls
    {
        ImGui::Text("Hardware: ");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##mode_hardware", mode_hardware == 0 ? "GPU" : "CPU"))
        {
            if (ImGui::Selectable("GPU", mode_hardware == 0))
                mode_hardware = 0;
            if (ImGui::Selectable("CPU", mode_hardware == 1))
                mode_hardware = 1;
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::Text("View: ");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##mode_view", mode_view == 0 ? "List" : "Timeline"))
        {
            if (ImGui::Selectable("List", mode_view == 0))
                mode_view = 0;
            if (ImGui::Selectable("Timeline", mode_view == 1))
                mode_view = 1;
            ImGui::EndCombo();
        }

        if (mode_view == 0)
        {
            ImGui::SameLine();
            ImGui::Text("Sort: ");
            ImGui::SameLine();
            if (ImGui::BeginCombo("##mode_sort", mode_sort == 0 ? "Alphabetically" : "By Duration"))
            {
                if (ImGui::Selectable("Alphabetically", mode_sort == 0))
                    mode_sort = 0;
                if (ImGui::Selectable("By Duration", mode_sort == 1))
                    mode_sort = 1;
                ImGui::EndCombo();
            }
        }

        // freeze toggle and update interval on the same line
        ImGui::Text("Freeze");
        ImGui::SameLine();
        ImGuiSp::toggle_switch("##freeze", &m_frozen);
        if (!m_frozen)
        {
            ImGui::SameLine();
            float interval = spartan::Profiler::GetUpdateInterval();
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##update_interval", &interval, 0.0f, 0.5f, "Update Interval = %.2f");
            spartan::Profiler::SetUpdateInterval(interval);
        }

        ImGui::Separator();
    }

    spartan::TimeBlockType type = mode_hardware == 0 ? spartan::TimeBlockType::Gpu : spartan::TimeBlockType::Cpu;

    // freeze: snapshot the current data and keep displaying it until unfrozen
    if (!m_frozen)
    {
        m_frozen_time_blocks = spartan::Profiler::GetTimeBlocks();
        m_frozen_time_cpu    = spartan::Profiler::GetTimeCpuLast();
        m_frozen_time_gpu    = spartan::Profiler::GetTimeGpuLast();
    }

    vector<spartan::TimeBlock>& time_blocks = m_frozen_time_blocks;
    uint32_t time_block_count               = static_cast<uint32_t>(time_blocks.size());
    float time_last                         = type == spartan::TimeBlockType::Cpu ? m_frozen_time_cpu : m_frozen_time_gpu;

    if (mode_view == 0)
    {
        // list view
        if (mode_sort == 1)
        {
            sort(time_blocks.begin(), time_blocks.end(), [](const spartan::TimeBlock& a, const spartan::TimeBlock& b)
            {
                return a.GetDuration() > b.GetDuration();
            });
        }
        else if (mode_sort == 0)
        {
            sort(time_blocks.begin(), time_blocks.end(), [](const spartan::TimeBlock& a, const spartan::TimeBlock& b)
            {
                return a.GetName() < b.GetName();
            });
        }

        for (uint32_t i = 0; i < time_block_count; i++)
        {
            if (time_blocks[i].GetType() != type)
                continue;
            if (!time_blocks[i].IsComplete())
                continue;
            show_time_block(time_blocks[i]);
        }
    }
    else
    {
        // timeline view

        // layout constants
        const float lane_height   = 40.0f;
        const float lane_padding  = 4.0f;
        const float label_width   = 120.0f;
        const float ruler_height  = 34.0f;
        const float content_width = ImGui::GetContentRegionAvail().x;
        const float timeline_width = ImMax(content_width - label_width, 100.0f);

        // build lane info
        struct LaneInfo
        {
            const char*              label;
            spartan::TimeBlockType   block_type;
            spartan::RHI_Queue_Type  queue_filter;
            bool                     use_depth;
        };

        vector<LaneInfo> lanes;
        uint32_t max_depth = 0;

        if (type == spartan::TimeBlockType::Gpu)
        {
            lanes.push_back({"Graphics", spartan::TimeBlockType::Gpu, spartan::RHI_Queue_Type::Graphics, false});
            lanes.push_back({"Compute",  spartan::TimeBlockType::Gpu, spartan::RHI_Queue_Type::Compute,  false});
        }
        else
        {
            for (uint32_t i = 0; i < time_block_count; i++)
            {
                if (time_blocks[i].GetType() == spartan::TimeBlockType::Cpu && time_blocks[i].IsComplete())
                    max_depth = max(max_depth, time_blocks[i].GetTreeDepth());
            }
            lanes.push_back({"CPU", spartan::TimeBlockType::Cpu, spartan::RHI_Queue_Type::Max, true});
        }

        // compute total timeline height for the invisible button
        float total_lanes_height = 0.0f;
        for (const auto& lane : lanes)
        {
            uint32_t depth_count = lane.use_depth ? (max_depth + 1) : 1;
            total_lanes_height += lane_height * depth_count + lane_padding;
        }
        float total_timeline_height = ruler_height + total_lanes_height;

        // compute the actual data extent across all visible blocks
        float data_min_ms = FLT_MAX;
        float data_max_ms = 0.0f;
        for (uint32_t i = 0; i < time_block_count; i++)
        {
            const spartan::TimeBlock& block = time_blocks[i];
            if (!block.IsComplete() || block.GetType() != type)
                continue;

            if (type == spartan::TimeBlockType::Gpu)
            {
                bool in_any_lane = false;
                for (const auto& lane : lanes)
                {
                    if (lane.queue_filter == spartan::RHI_Queue_Type::Max || block.GetQueueType() == lane.queue_filter)
                    {
                        in_any_lane = true;
                        break;
                    }
                }
                if (!in_any_lane)
                    continue;
            }

            data_min_ms = ImMin(data_min_ms, block.GetStartMs());
            data_max_ms = ImMax(data_max_ms, block.GetEndMs());
        }
        if (data_min_ms == FLT_MAX)
        {
            data_min_ms = 0.0f;
            data_max_ms = 16.67f;
        }
        float data_extent = ImMax(data_max_ms - data_min_ms, 0.5f);

        // auto-fit on first view or mode change
        if (m_timeline_needs_fit && data_max_ms > 0.0f)
        {
            m_timeline_offset_ms  = ImMax(data_min_ms - data_extent * 0.02f, 0.0f);
            m_timeline_range_ms   = data_extent * 1.05f;
            m_timeline_needs_fit  = false;
            m_user_has_interacted = false;
        }

        // auto-grow: only when the user hasn't manually zoomed or panned
        if (!m_user_has_interacted)
        {
            float visible_end = m_timeline_offset_ms + m_timeline_range_ms;
            if (data_max_ms > visible_end)
            {
                m_timeline_range_ms = (data_max_ms - m_timeline_offset_ms) * 1.05f;
            }
        }

        // cap range to something sane (200ms = ~5fps, anything beyond is garbage data)
        m_timeline_range_ms = ImClamp(m_timeline_range_ms, 0.01f, 200.0f);

        // capture the origin before any drawing so zoom/pan math is stable
        ImVec2 timeline_screen_origin = ImGui::GetCursorScreenPos();

        // place an invisible button over the entire timeline area for input capture
        ImGui::InvisibleButton("##timeline_input", ImVec2(content_width, total_timeline_height));
        bool timeline_hovered = ImGui::IsItemHovered();
        bool timeline_active  = ImGui::IsItemActive();

        // zoom with scroll wheel
        if (timeline_hovered)
        {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                m_user_has_interacted = true;

                float zoom_factor = 1.0f - wheel * 0.15f;
                zoom_factor = ImClamp(zoom_factor, 0.5f, 2.0f);

                float mouse_x    = ImGui::GetIO().MousePos.x - timeline_screen_origin.x - label_width;
                float mouse_frac = ImClamp(mouse_x / timeline_width, 0.0f, 1.0f);
                float mouse_ms   = m_timeline_offset_ms + mouse_frac * m_timeline_range_ms;
                float new_range  = m_timeline_range_ms * zoom_factor;
                new_range        = ImClamp(new_range, 0.01f, 200.0f);

                m_timeline_offset_ms = mouse_ms - mouse_frac * new_range;
                m_timeline_range_ms  = new_range;
            }
        }

        // pan with right-click drag or middle-click drag
        if (timeline_hovered || timeline_active)
        {
            bool dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
            if (dragging)
            {
                m_user_has_interacted = true;

                float drag_delta_x = ImGui::GetIO().MouseDelta.x;
                float ms_per_pixel = m_timeline_range_ms / timeline_width;
                m_timeline_offset_ms -= drag_delta_x * ms_per_pixel;
            }
        }

        m_timeline_offset_ms = ImMax(m_timeline_offset_ms, 0.0f);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 origin         = timeline_screen_origin;

        // draw ruler background
        {
            ImVec2 ruler_min = ImVec2(origin.x + label_width, origin.y);
            ImVec2 ruler_max = ImVec2(ruler_min.x + timeline_width, ruler_min.y + ruler_height);
            draw_list->AddRectFilled(ruler_min, ruler_max, IM_COL32(35, 35, 40, 255));

            // label area background
            draw_list->AddRectFilled(origin, ImVec2(origin.x + label_width - 1.0f, ruler_max.y), IM_COL32(35, 35, 40, 255));
            draw_list->AddText(ImVec2(origin.x + 8.0f, origin.y + 8.0f), IM_COL32(140, 140, 140, 255), "ms");

            // vertical divider between labels and ruler
            draw_list->AddLine(
                ImVec2(origin.x + label_width - 1.0f, origin.y),
                ImVec2(origin.x + label_width - 1.0f, ruler_max.y),
                IM_COL32(65, 65, 70, 255)
            );

            // tick marks
            float ms_per_pixel   = m_timeline_range_ms / timeline_width;
            float target_tick_ms = ms_per_pixel * 100.0f;

            float nice_intervals[] = { 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f, 250.0f, 500.0f };
            float tick_interval_ms = nice_intervals[sizeof(nice_intervals) / sizeof(nice_intervals[0]) - 1]; // default to largest
            for (float iv : nice_intervals)
            {
                if (iv >= target_tick_ms)
                {
                    tick_interval_ms = iv;
                    break;
                }
            }

            float first_tick    = floor(m_timeline_offset_ms / tick_interval_ms) * tick_interval_ms;
            uint32_t tick_count = 0;
            for (float tick_ms = first_tick; tick_ms <= m_timeline_offset_ms + m_timeline_range_ms; tick_ms += tick_interval_ms)
            {
                if (++tick_count > 500)
                    break;

                float frac = (tick_ms - m_timeline_offset_ms) / m_timeline_range_ms;
                if (frac < -0.01f || frac > 1.01f)
                    continue;

                float x = ruler_min.x + frac * timeline_width;

                // vertical grid line through the whole timeline (not clipped)
                draw_list->AddLine(
                    ImVec2(x, ruler_max.y),
                    ImVec2(x, origin.y + total_timeline_height),
                    IM_COL32(50, 50, 55, 255)
                );

                // tick line on ruler
                draw_list->AddLine(
                    ImVec2(x, ruler_min.y + ruler_height * 0.55f),
                    ImVec2(x, ruler_max.y),
                    IM_COL32(130, 130, 130, 255)
                );
            }

            // draw tick labels clipped to ruler area so they don't overflow on the right
            draw_list->PushClipRect(ruler_min, ruler_max, true);
            tick_count = 0;
            for (float tick_ms = first_tick; tick_ms <= m_timeline_offset_ms + m_timeline_range_ms; tick_ms += tick_interval_ms)
            {
                if (++tick_count > 500)
                    break;

                float frac = (tick_ms - m_timeline_offset_ms) / m_timeline_range_ms;
                if (frac < -0.01f || frac > 1.01f)
                    continue;

                float x = ruler_min.x + frac * timeline_width;

                char tick_label[32];
                if (tick_interval_ms >= 1.0f)
                    snprintf(tick_label, sizeof(tick_label), "%.0f", tick_ms);
                else
                    snprintf(tick_label, sizeof(tick_label), "%.2f", tick_ms);

                draw_list->AddText(ImVec2(x + 3.0f, ruler_min.y + 4.0f), IM_COL32(180, 180, 180, 255), tick_label);
            }
            draw_list->PopClipRect();

            // ruler bottom border
            draw_list->AddLine(ImVec2(origin.x, ruler_max.y), ImVec2(origin.x + content_width, ruler_max.y), IM_COL32(80, 80, 80, 255));
        }

        // draw each lane
        const spartan::TimeBlock* tooltip_block = nullptr;
        float tooltip_block_width               = FLT_MAX;
        float y_cursor = origin.y + ruler_height;
        for (size_t lane_idx = 0; lane_idx < lanes.size(); lane_idx++)
        {
            const auto& lane = lanes[lane_idx];

            uint32_t lane_depth_count = lane.use_depth ? (max_depth + 1) : 1;
            float total_lane_height   = lane_height * lane_depth_count;

            // lane label area
            draw_list->AddRectFilled(
                ImVec2(origin.x, y_cursor),
                ImVec2(origin.x + label_width - 1.0f, y_cursor + total_lane_height),
                IM_COL32(38, 38, 42, 255)
            );

            // label text (vertically centered, with padding from the right edge)
            float text_y = y_cursor + (total_lane_height - ImGui::GetTextLineHeight()) * 0.5f;
            draw_list->AddText(ImVec2(origin.x + 8.0f, text_y), IM_COL32(210, 210, 210, 255), lane.label);

            // vertical divider between labels and timeline
            draw_list->AddLine(
                ImVec2(origin.x + label_width - 1.0f, y_cursor),
                ImVec2(origin.x + label_width - 1.0f, y_cursor + total_lane_height),
                IM_COL32(65, 65, 70, 255)
            );

            // lane background with alternating shade
            ImU32 lane_bg = (lane_idx % 2 == 0) ? IM_COL32(22, 22, 28, 255) : IM_COL32(28, 28, 34, 255);
            ImVec2 lane_origin = ImVec2(origin.x + label_width, y_cursor);
            draw_list->AddRectFilled(lane_origin, ImVec2(lane_origin.x + timeline_width, y_cursor + total_lane_height), lane_bg);

            // lane separator line (horizontal)
            draw_list->AddLine(
                ImVec2(origin.x, y_cursor + total_lane_height),
                ImVec2(origin.x + content_width, y_cursor + total_lane_height),
                IM_COL32(55, 55, 60, 255)
            );

            // draw time blocks for this lane
            for (uint32_t i = 0; i < time_block_count; i++)
            {
                const spartan::TimeBlock& block = time_blocks[i];
                if (!block.IsComplete() || block.GetType() != lane.block_type)
                    continue;

                // filter by queue type for gpu lanes
                if (lane.block_type == spartan::TimeBlockType::Gpu && lane.queue_filter != spartan::RHI_Queue_Type::Max)
                {
                    if (block.GetQueueType() != lane.queue_filter)
                        continue;
                }

                float block_start = block.GetStartMs();
                float block_end   = block.GetEndMs();

                // skip blocks entirely outside visible range
                if (block_end < m_timeline_offset_ms || block_start > m_timeline_offset_ms + m_timeline_range_ms)
                    continue;

                // compute pixel positions
                float frac_start = (block_start - m_timeline_offset_ms) / m_timeline_range_ms;
                float frac_end   = (block_end - m_timeline_offset_ms) / m_timeline_range_ms;
                frac_start       = ImClamp(frac_start, 0.0f, 1.0f);
                frac_end         = ImClamp(frac_end, 0.0f, 1.0f);

                float x0 = lane_origin.x + frac_start * timeline_width;
                float x1 = lane_origin.x + frac_end * timeline_width;

                // minimum width so tiny blocks are still visible and clickable
                if (x1 - x0 < 3.0f)
                    x1 = x0 + 3.0f;

                // vertical position
                float depth_offset = lane.use_depth ? (block.GetTreeDepth() * lane_height) : 0.0f;
                float y0 = y_cursor + depth_offset + 2.0f;
                float y1 = y0 + lane_height - 4.0f;

                bool is_compute = (block.GetQueueType() == spartan::RHI_Queue_Type::Compute);
                ImU32 col = get_time_block_color(block.GetName(), is_compute);

                // draw block
                draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col, 2.0f);

                // subtle border for depth
                draw_list->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(0, 0, 0, 60), 2.0f);

                // text label: show "name - Xms" if wide enough, just name if moderate, clipped if narrow
                float block_width = x1 - x0;
                const char* name  = block.GetName();

                char full_label[128];
                snprintf(full_label, sizeof(full_label), "%s - %.2fms", name, block.GetDuration());

                ImVec2 full_size = ImGui::CalcTextSize(full_label);
                ImVec2 name_size = ImGui::CalcTextSize(name);

                float text_y_offset = y0 + (lane_height - 4.0f - ImGui::GetTextLineHeight()) * 0.5f;

                if (block_width > full_size.x + 6.0f)
                {
                    draw_list->AddText(ImVec2(x0 + 3.0f, text_y_offset), IM_COL32(255, 255, 255, 240), full_label);
                }
                else if (block_width > name_size.x + 6.0f)
                {
                    draw_list->AddText(ImVec2(x0 + 3.0f, text_y_offset), IM_COL32(255, 255, 255, 240), name);
                }
                else if (block_width > 8.0f)
                {
                    draw_list->PushClipRect(ImVec2(x0 + 1.0f, y0), ImVec2(x1 - 1.0f, y1), true);
                    draw_list->AddText(ImVec2(x0 + 3.0f, text_y_offset), IM_COL32(255, 255, 255, 200), name);
                    draw_list->PopClipRect();
                }

                // track the narrowest block under the cursor for tooltip
                if (timeline_hovered && ImGui::IsMouseHoveringRect(ImVec2(x0, y0), ImVec2(x1, y1)))
                {
                    if (tooltip_block == nullptr || block_width < tooltip_block_width)
                    {
                        tooltip_block       = &block;
                        tooltip_block_width = block_width;
                    }
                }
            }

            y_cursor += total_lane_height + lane_padding;
        }

        // show tooltip for the narrowest hovered block
        if (tooltip_block)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(tooltip_block->GetName());
            ImGui::Separator();
            ImGui::Text("duration: %.3f ms", tooltip_block->GetDuration());
            ImGui::Text("start:    %.3f ms", tooltip_block->GetStartMs());
            ImGui::Text("end:      %.3f ms", tooltip_block->GetEndMs());
            if (tooltip_block->GetType() == spartan::TimeBlockType::Gpu)
            {
                const char* queue_name = "unknown";
                if (tooltip_block->GetQueueType() == spartan::RHI_Queue_Type::Graphics)
                    queue_name = "graphics";
                else if (tooltip_block->GetQueueType() == spartan::RHI_Queue_Type::Compute)
                    queue_name = "compute";
                ImGui::Text("queue:    %s", queue_name);
            }
            ImGui::EndTooltip();
        }

        // outer border around the entire timeline
        draw_list->AddRect(origin, ImVec2(origin.x + content_width, origin.y + total_timeline_height), IM_COL32(70, 70, 75, 255));

        // info bar below the timeline
        ImGui::Text("%.2f - %.2f ms (%.2f ms visible)", m_timeline_offset_ms, m_timeline_offset_ms + m_timeline_range_ms, m_timeline_range_ms);
        ImGui::SameLine();
        if (ImGuiSp::button("Fit"))
        {
            m_timeline_needs_fit  = true;
            m_user_has_interacted = false;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("scroll: zoom | right-drag: pan");
    }

    // plot (always uses live data regardless of freeze)
    ImGui::Separator();
    {
        float time_live = type == spartan::TimeBlockType::Cpu ? spartan::Profiler::GetTimeCpuLast() : spartan::Profiler::GetTimeGpuLast();

        if (previous_item_type != mode_hardware)
        {
            m_plot.fill(0.0f);
            m_timings.Clear();
        }

        if (time_live == 0.0f)
        {
            time_live = m_plot.back();
        }
        else
        {
            m_timings.AddSample(time_live);
        }

        // cur, avg, min, max
        {
            if (ImGuiSp::button("Clear"))
            {
                m_timings.Clear();
            }
            ImGui::SameLine();
            ImGui::Text("Cur:%.2f, Avg:%.2f, Min:%.2f, Max:%.2f", time_live, m_timings.m_avg, m_timings.m_min, m_timings.m_max);
            bool is_stuttering = type == spartan::TimeBlockType::Cpu ? spartan::Profiler::IsCpuStuttering() : spartan::Profiler::IsGpuStuttering();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(is_stuttering ? 1.0f : 0.0f, is_stuttering ? 0.0f : 1.0f, 0.0f, 1.0f), is_stuttering ? "Stuttering: Yes" : "Stuttering: No");
        }

        for (uint32_t i = 0; i < m_plot.size() - 1; i++)
        {
            m_plot[i] = m_plot[i + 1];
        }
        m_plot[m_plot.size() - 1] = time_live;

        ImGui::PlotLines("##performance_plot", m_plot.data(), static_cast<int>(m_plot.size()), 0, "", m_timings.m_min, m_timings.m_max, ImVec2(ImGui::GetContentRegionAvail().x, 80));
    }

    // memory (vram/ram)
    {
        ImGui::Separator();

        bool is_vram    = type == spartan::TimeBlockType::Gpu;
        float allocated = is_vram ? spartan::RHI_Device::MemoryGetAllocatedMb() : spartan::Allocator::GetMemoryAllocatedMb();
        float available = is_vram ? spartan::RHI_Device::MemoryGetAvailableMb() : spartan::Allocator::GetMemoryAvailableMb();
        float total     = is_vram ? spartan::RHI_Device::MemoryGetTotalMb()     : spartan::Allocator::GetMemoryTotalMb();

        show_memory_bar(is_vram ? "VRAM" : "RAM", allocated, available, total, ImVec2(-1, 32));
    }
}
