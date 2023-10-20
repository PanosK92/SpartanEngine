/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =======================
#include "Profiler.h"
#include "../ImGui/ImGuiExtension.h"
//==================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace
{
    void show_time_block(const Spartan::TimeBlock& time_block)
    {
        float m_tree_depth_stride = 10;

        const char* name        = time_block.GetName();
        const float duration    = time_block.GetDuration();
        const float fraction    = duration / 10.0f;
        const float width       = fraction * ImGuiSp::GetWindowContentRegionWidth();
        const auto& color       = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
        const ImVec2 pos_screen = ImGui::GetCursorScreenPos();
        const ImVec2 pos        = ImGui::GetCursorPos();
        const float text_height = ImGui::CalcTextSize(name, nullptr, true).y;

        // rectangle
        ImGui::GetWindowDrawList()->AddRectFilled(pos_screen, ImVec2(pos_screen.x + width, pos_screen.y + text_height), IM_COL32(color.x * 255, color.y * 255, color.z * 255, 255));

        // text
        ImGui::SetCursorPos(ImVec2(pos.x + m_tree_depth_stride * time_block.GetTreeDepth(), pos.y));
        ImGui::Text("%s - %.2f ms", name, duration);
    }
}

Profiler::Profiler(Editor* editor) : Widget(editor)
{
    m_flags        |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar;
    m_title        = "Profiler";
    m_visible      = false;
    m_size_initial = Vector2(1000, 715);
}

void Profiler::OnVisible()
{
    Spartan::Profiler::SetEnabled(true);
}

void Profiler::OnHidden()
{
    Spartan::Profiler::SetEnabled(false);
}

void Profiler::OnTickVisible()
{
    int previous_item_type = m_item_type;

    ImGui::RadioButton("CPU", &m_item_type, 0);
    ImGui::SameLine();
    ImGui::RadioButton("GPU", &m_item_type, 1);
    ImGui::SameLine();
    float interval = Spartan::Profiler::GetUpdateInterval();
    ImGui::DragFloat("Update interval", &interval, 0.001f, 0.0f, 0.5f);
    Spartan::Profiler::SetUpdateInterval(interval);
    ImGui::Separator();

    Spartan::TimeBlockType type            = m_item_type == 0 ? Spartan::TimeBlockType::Cpu : Spartan::TimeBlockType::Gpu;
    vector<Spartan::TimeBlock> time_blocks = Spartan::Profiler::GetTimeBlocks();
    uint32_t time_block_count              = static_cast<uint32_t>(time_blocks.size());
    float time_last                        = type == Spartan::TimeBlockType::Cpu ? Spartan::Profiler::GetTimeCpuLast() : Spartan::Profiler::GetTimeGpuLast();

    // sort time_blocks by duration, descending
    sort(time_blocks.begin(), time_blocks.end(), [](const Spartan::TimeBlock& a, const Spartan::TimeBlock& b)
    {
        return b.GetDuration() < a.GetDuration();
    });

    // time blocks
    for (uint32_t i = 0; i < time_block_count; i++)
    {
        if (time_blocks[i].GetType() != type)
            continue;

        if (!time_blocks[i].IsComplete())
            return;

        show_time_block(time_blocks[i]);
    }

    // plot
    ImGui::Separator();
    {
        // clear plot on change from cpu to gpu and vice versa
        if (previous_item_type != m_item_type)
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
            if (ImGuiSp::button("Clear")) { m_timings.Clear(); }
            ImGui::SameLine();
            ImGui::Text("Cur:%.2f, Avg:%.2f, Min:%.2f, Max:%.2f", time_last, m_timings.m_avg, m_timings.m_min, m_timings.m_max);
            bool is_stuttering = type == Spartan::TimeBlockType::Cpu ? Spartan::Profiler::IsCpuStuttering() : Spartan::Profiler::IsGpuStuttering();
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
        ImGui::PlotLines("", m_plot.data(), static_cast<int>(m_plot.size()), 0, "", m_timings.m_min, m_timings.m_max, ImVec2(ImGuiSp::GetWindowContentRegionWidth(), 80));
    }

    // vram
    if (type == Spartan::TimeBlockType::Gpu)
    {
        ImGui::Separator();

        const uint32_t memory_used      = Spartan::Profiler::GpuGetMemoryUsed();
        const uint32_t memory_available = Spartan::Profiler::GpuGetMemoryAvailable();
        const string overlay            = "Memory " + to_string(memory_used) + "/" + to_string(memory_available) + " MB";

        ImGui::ProgressBar((float)memory_used / (float)memory_available, ImVec2(-1, 0), overlay.c_str());
    }
}
