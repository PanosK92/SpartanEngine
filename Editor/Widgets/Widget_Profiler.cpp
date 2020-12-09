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

//= INCLUDES ===============
#include "Widget_Profiler.h"
#include "Math/Vector3.h"
#include "Core/Context.h"
#include "Math/Vector2.h"
//==========================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

Widget_Profiler::Widget_Profiler(Editor* editor) : Widget(editor)
{
    m_flags         |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar;
    m_title         = "Profiler";
    m_is_visible    = false;
    m_profiler      = m_context->GetSubsystem<Profiler>();
    m_size          = Vector2(1000, 715);
}

void Widget_Profiler::OnShow()
{
    m_profiler->SetEnabled(true);
}

void Widget_Profiler::OnHide()
{
    m_profiler->SetEnabled(false);
}

static void ShowTimeBlock(const TimeBlock& time_block, float total_time)
{
    if (!time_block.IsComplete())
        return;

    float m_tree_depth_stride = 10;

    const char* name        = time_block.GetName();
    const float duration    = time_block.GetDuration();
    const float fraction    = duration / total_time;
    const float width       = fraction * ImGui::GetWindowContentRegionWidth();
    const auto& color       = ImGui::GetStyle().Colors[ImGuiCol_FrameBgActive];
    const ImVec2 pos_screen = ImGui::GetCursorScreenPos();
    const ImVec2 pos        = ImGui::GetCursorPos();
    const float text_height = ImGui::CalcTextSize(name, nullptr, true).y;

    // Rectangle
    ImGui::GetWindowDrawList()->AddRectFilled(pos_screen, ImVec2(pos_screen.x + width, pos_screen.y + text_height), IM_COL32(color.x * 255, color.y * 255, color.z * 255, 255));
    // Text
    ImGui::SetCursorPos(ImVec2(pos.x + m_tree_depth_stride * time_block.GetTreeDepth(), pos.y));
    ImGui::Text("%s - %.2f ms", name, duration);
}

void Widget_Profiler::TickVisible()
{
    int previous_item_type = m_item_type;

    ImGui::RadioButton("CPU", &m_item_type, 0);
    ImGui::SameLine();
    ImGui::RadioButton("GPU", &m_item_type, 1);
    ImGui::SameLine();
    float interval = m_profiler->GetUpdateInterval();
    ImGui::DragFloat("Update interval (The smaller the interval the higher the performance impact)", &interval, 0.001f, 0.0f, 0.5f);
    m_profiler->SetUpdateInterval(interval);
    ImGui::Separator();

    TimeBlockType type                          = m_item_type == 0 ? TimeBlockType::Cpu : TimeBlockType::Gpu;
    const std::vector<TimeBlock>& time_blocks   = m_profiler->GetTimeBlocks();
    const uint32_t time_block_count             = static_cast<uint32_t>(time_blocks.size());
    float time_last                             = type == TimeBlockType::Cpu ? m_profiler->GetTimeCpuLast() : m_profiler->GetTimeGpuLast();

    // Time blocks
    for (uint32_t i = 0; i < time_block_count; i++)
    {
        if (time_blocks[i].GetType() != type)
            continue;

        ShowTimeBlock(time_blocks[i], time_last);
    }

    // Plot
    ImGui::Separator();
    {
        // Clear plot on change from cpu to gpu and vice versa
        if (previous_item_type != m_item_type)
        {
            m_plot.fill(0.0f);
            m_timings.Clear();
        }

        // If the update frequency is low enough, we can get zeros, in this case simply repeat the last value
        if (time_last == 0.0f)
        {
            time_last = m_plot.back();
        }
        else
        {
            m_timings.AddSample(time_last);
        }

        // Cur, Avg, Min, Max
        {
            if (ImGui::Button("Clear")) { m_timings.Clear(); }
            ImGui::SameLine();
            ImGui::Text("Cur:%.2f, Avg:%.2f, Min:%.2f, Max:%.2f", time_last, m_timings.m_avg, m_timings.m_min, m_timings.m_max);
            bool is_stuttering = type == TimeBlockType::Cpu ? m_profiler->IsCpuStuttering() : m_profiler->IsGpuStuttering();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(is_stuttering ? 1.0f : 0.0f, is_stuttering ? 0.0f : 1.0f, 0.0f, 1.0f), is_stuttering ? "Stuttering: Yes" : "Stuttering: No");
        }

        // Shift plot to the left
        for (uint32_t i = 0; i < m_plot.size() - 1; i++)
        {
            m_plot[i] = m_plot[i + 1];
        }

        // Update last entry
        m_plot[m_plot.size() - 1] = time_last;

        // Show
        ImGui::PlotLines("", m_plot.data(), static_cast<int>(m_plot.size()), 0, "", m_timings.m_min, m_timings.m_max, ImVec2(ImGui::GetWindowContentRegionWidth(), 80));
    }

    // VRAM
    if (type == TimeBlockType::Gpu)
    {
        ImGui::Separator();

        const uint32_t memory_used      = m_profiler->GpuGetMemoryUsed();
        const uint32_t memory_available = m_profiler->GpuGetMemoryAvailable();
        const string overlay            = "Memory " + to_string(memory_used) + "/" + to_string(memory_available) + " MB";

        ImGui::ProgressBar((float)memory_used / (float)memory_available, ImVec2(-1, 0), overlay.c_str());
    }
}
