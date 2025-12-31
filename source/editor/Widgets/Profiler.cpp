/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include "Profiling/CsvExporter.h"
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

        // Generate a unique color based on name hash
        size_t hash_value = hash<string>{}(name);
        float hue = static_cast<float>(hash_value % 360) / 360.0f;
        ImVec4 color = ImColor::HSV(hue, 0.6f, 0.6f);

        // rectangle
        ImGui::GetWindowDrawList()->AddRectFilled(pos_screen, ImVec2(pos_screen.x + width, pos_screen.y + text_height), IM_COL32(color.x * 255, color.y * 255, color.z * 255, 255));

        // text
        ImGui::SetCursorPos(ImVec2(pos.x + m_tree_depth_stride * time_block.GetTreeDepth(), pos.y));
        ImGui::Text("%s - %.2f ms", name, duration);
    }

    void show_memory_bar(const char* label, float used_mb, float budget_mb, float total_mb, ImVec2 size = ImVec2(-1, 0))
    {
        ImVec2 pos  = ImGui::GetCursorScreenPos();
        float fullW = (size.x <= 0.0f ? ImGui::GetContentRegionAvail().x : size.x);
        float fullH = (size.y <= 0.0f ? ImGui::GetTextLineHeightWithSpacing() : size.y);
    
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
        // define colors
        ImU32 col_total  = IM_COL32(20, 30, 60, 255);    // deep blue for total memory
        ImU32 col_budget = IM_COL32(80, 150, 220, 255);  // medium blue for budget
        // interpolate used color from green -> yellow -> red
        float usedFrac = (budget_mb > 0.0f) ? (used_mb / budget_mb) : 0.0f;
        usedFrac = ImClamp(usedFrac, 0.0f, 1.0f);
        ImU32 col_used;
        if (usedFrac < 0.5f)
        {
            // green to yellow
            float t = usedFrac / 0.5f;
            col_used = IM_COL32(
                (int)(80  + t * (220-80)),  // R: 80->220
                (int)(220 - t * (220-180)), // G: 220->180
                80,                          // B constant
                255
            );
        }
        else
        {
            // yellow to red
            float t = (usedFrac-0.5f)/0.5f;
            col_used = IM_COL32(
                220,                         // R constant
                (int)(180 - t * 180),        // G: 180->0
                80,                          // B constant
                255
            );
        }
    
        // background (total memory)
        draw_list->AddRectFilled(pos, ImVec2(pos.x + fullW, pos.y + fullH), col_total);
    
        // budget (on top of total)
        float budgetFrac = (budget_mb > 0.0f && total_mb > 0.0f) ? (budget_mb / total_mb) : 0.0f;
        draw_list->AddRectFilled(pos, ImVec2(pos.x + fullW * budgetFrac, pos.y + fullH), col_budget);
    
        // used (on top of budget)
        draw_list->AddRectFilled(pos, ImVec2(pos.x + fullW * usedFrac * budgetFrac, pos.y + fullH), col_used);
    
        // border
        draw_list->AddRect(pos, ImVec2(pos.x + fullW, pos.y + fullH), IM_COL32(255, 255, 255, 255));
    
        // text overlay
        char buf[128];
        snprintf(buf, sizeof(buf), "%s %.0f/%.0f MB (Budget %.0f MB)", label, used_mb, total_mb, budget_mb);
        ImGui::RenderTextClipped(pos, ImVec2(pos.x + fullW, pos.y + fullH), buf, nullptr, nullptr, ImVec2(0.5f, 0.5f));
    
        ImGui::Dummy(ImVec2(fullW, fullH)); // advance cursor
    }

    int mode_hardware = 0; // 0: gpu, 1: cpu
    int mode_sort     = 1; // 0: alphabetically, 1: by duration
}

Profiler::Profiler(Editor* editor) : Widget(editor)
{
    m_flags        |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar;
    m_title        = "Profiler";
    m_visible      = false;
    m_size_initial = Vector2(1000, 715);
    m_plot.fill(16.0f);
}

void Profiler::OnTickVisible()
{
    int previous_item_type = mode_hardware;

    // controls
    {
        ImGui::Text("Hardware: ");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##mode_hardware", mode_hardware == 0 ? "GPU" : "CPU"))
        {
            if (ImGui::Selectable("GPU", mode_hardware == 0))
            {
                mode_hardware = 0;
                spartan::CsvExporter::StopRecording(true);
            }

            if (ImGui::Selectable("CPU", mode_hardware == 1))
            {
                mode_hardware = 1;
                spartan::CsvExporter::StopRecording(true);
            }

            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::Text("Sort: ");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##mode_sort", mode_sort == 0 ? "Alphabetically" : "By Duration"))
        {
            if (ImGui::Selectable("Alphabetically", mode_sort == 0))
            {
                mode_sort = 0;
                spartan::CsvExporter::StopRecording(true);
            }

            if (ImGui::Selectable("By Duration", mode_sort == 1))
            {
                mode_sort = 1;
                spartan::CsvExporter::StopRecording(true);
            }

            ImGui::EndCombo();
        }

        float interval = spartan::Profiler::GetUpdateInterval();
        ImGui::SetNextItemWidth(-1); // use all available horizontal space
        ImGui::SliderFloat("##update_interval", &interval, 0.0f, 0.5f, "Update Interval = %.2f");
        spartan::Profiler::SetUpdateInterval(interval);

        ImGui::Text("CSV:");
        ImGui::SameLine();
        if (ImGui::Button("Start Recording"))
        {
            spartan::CsvExporter::StartRecording(mode_hardware);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop Recording"))
        {
            spartan::CsvExporter::StopRecording();
        }

        ImGui::Separator();
    }

    spartan::TimeBlockType type            = mode_hardware == 0 ? spartan::TimeBlockType::Gpu : spartan::TimeBlockType::Cpu;
    vector<spartan::TimeBlock> time_blocks = spartan::Profiler::GetTimeBlocks();
    uint32_t time_block_count              = static_cast<uint32_t>(time_blocks.size());
    float time_last                        = type == spartan::TimeBlockType::Cpu ? spartan::Profiler::GetTimeCpuLast() : spartan::Profiler::GetTimeGpuLast();

    if (mode_sort == 1) // sort by Duration, descending
    {
        sort(time_blocks.begin(), time_blocks.end(), [](const spartan::TimeBlock& a, const spartan::TimeBlock& b)
        {
            return a.GetDuration() > b.GetDuration();
        });
    }
    else if (mode_sort == 0) // sort Alphabetically
    {
        sort(time_blocks.begin(), time_blocks.end(), [](const spartan::TimeBlock& a, const spartan::TimeBlock& b)
        {
            return a.GetName() < b.GetName();
        });
    }

    // time blocks
    for (uint32_t i = 0; i < time_block_count; i++)
    {
        if (time_blocks[i].GetType() != type)
            continue;

        if (!time_blocks[i].IsComplete())
            continue;

        show_time_block(time_blocks[i]);

        // Csv recording
        spartan::CsvExporter::WriteFrameData(time_blocks[i], spartan::Renderer::GetFrameNumber());
    }
    spartan::CsvExporter::NextFrame();

    // plot
    ImGui::Separator();
    {
        // clear plot on change from cpu to gpu and vice versa
        if (previous_item_type != mode_hardware)
        {
            m_plot.fill(0.0f);
            m_timings.Clear();
        }

        // if the update frequency is low enough, we can get zeros, in this case simply repeat the last value
        if (time_last == 0.0f)
        {
            time_last = m_plot.back();
        }
        else
        {
            m_timings.AddSample(time_last);
        }

        // cur, avg, min, max
        {
            if (ImGuiSp::button("Clear"))
            {
                m_timings.Clear();
            }
            ImGui::SameLine();
            ImGui::Text("Cur:%.2f, Avg:%.2f, Min:%.2f, Max:%.2f", time_last, m_timings.m_avg, m_timings.m_min, m_timings.m_max);
            bool is_stuttering = type == spartan::TimeBlockType::Cpu ? spartan::Profiler::IsCpuStuttering() : spartan::Profiler::IsGpuStuttering();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(is_stuttering ? 1.0f : 0.0f, is_stuttering ? 0.0f : 1.0f, 0.0f, 1.0f), is_stuttering ? "Stuttering: Yes" : "Stuttering: No");
        }

        // shift plot to the left
        for (uint32_t i = 0; i < m_plot.size() - 1; i++)
        {
            m_plot[i] = m_plot[i + 1];
        }

        // update last entry
        m_plot[m_plot.size() - 1] = time_last;

        // show
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
