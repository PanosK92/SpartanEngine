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
    m_title            = "Profiler";
    m_is_visible    = false;
    m_profiler        = m_context->GetSubsystem<Profiler>();
    m_size          = Vector2(1000, 715);

    m_plot_times_cpu.resize(m_plot_size);
    m_plot_times_gpu.resize(m_plot_size);
}

void Widget_Profiler::Tick()
{
    static int item_type = 1;
    ImGui::RadioButton("CPU", &item_type, 0);
    ImGui::SameLine();
    ImGui::RadioButton("GPU", &item_type, 1);
    ImGui::SameLine();
    float interval = m_profiler->GetUpdateInterval();
    ImGui::DragFloat("Update interval (The smaller the interval the higher the performance impact)", &interval, 0.001f, 0.0f, 0.5f);
    m_profiler->SetUpdateInterval(interval);
    ImGui::Separator();
    const bool show_cpu = (item_type == 0);

    if (show_cpu)
    {
        ShowCPU();
    }
    else
    {
        ShowGPU();
    }
}

void Widget_Profiler::ShowCPU()
{
    // Get stuff
    const auto& time_blocks        = m_profiler->GetTimeBlocks();
    const auto time_block_count = static_cast<unsigned int>(time_blocks.size());
    const auto time_cpu            = m_profiler->GetTimeCpuLast();    

    // Time blocks    
    for (unsigned int i = 0; i < time_block_count; i++)
    {
        if (time_blocks[i].GetType() != TimeBlock_Cpu)
            continue;

        ShowTimeBlock(time_blocks[i], time_cpu);
    }

    ImGui::Separator();
    ShowPlot(m_plot_times_cpu, m_metric_cpu, time_cpu, m_profiler->IsCpuStuttering());
}

void Widget_Profiler::ShowGPU()
{
    // Get stuff
    const auto& time_blocks        = m_profiler->GetTimeBlocks();
    const auto time_block_count    = static_cast<unsigned int>(time_blocks.size());
    const auto time_gpu            = m_profiler->GetTimeGpuLast();

    // Time blocks
    for (unsigned int i = 0; i < time_block_count; i++)
    {
        if (time_blocks[i].GetType() != TimeBlock_Gpu)
            continue;

        ShowTimeBlock(time_blocks[i], time_gpu);
    }

    // Plot
    ImGui::Separator();
    ShowPlot(m_plot_times_gpu, m_metric_gpu, time_gpu, m_profiler->IsGpuStuttering());

    // VRAM    
    ImGui::Separator();
    const unsigned int memory_used        = m_profiler->GpuGetMemoryUsed();
    const unsigned int memory_available    = m_profiler->GpuGetMemoryAvailable();
    const string overlay                = "Memory " + to_string(memory_used) + "/" + to_string(memory_available) + " MB";
    ImGui::ProgressBar((float)memory_used / (float)memory_available, ImVec2(-1, 0), overlay.c_str());
}

void Widget_Profiler::ShowTimeBlock(const TimeBlock& time_block, float total_time) const
{
    if (!time_block.IsComplete())
        return;

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

void Widget_Profiler::ShowPlot(vector<float>& data, Metric& metric, float time_value, bool is_stuttering) const
{
    if (time_value >= 0.0f)
    {
        metric.AddSample(time_value);
    }
    else
    {
        // Repeat the last value
        time_value = data.back();
    }

    // Add value to data
    data.erase(data.begin());
    data.emplace_back(time_value);

    if (ImGui::Button("Clear")) { metric.Clear(); }
    ImGui::SameLine();  ImGui::Text("Cur:%.2f, Avg:%.2f, Min:%.2f, Max:%.2f", time_value, metric.m_avg, metric.m_min, metric.m_max);
    ImGui::SameLine();  ImGui::TextColored(ImVec4(is_stuttering ? 1.0f : 0.0f, is_stuttering ? 0.0f : 1.0f, 0.0f, 1.0f), is_stuttering ? "Stuttering: Yes" : "Stuttering: No");

    // Plot data
    ImGui::PlotLines("", data.data(), static_cast<int>(data.size()), 0, "", metric.m_min, metric.m_max, ImVec2(ImGui::GetWindowContentRegionWidth(), 80));
}
