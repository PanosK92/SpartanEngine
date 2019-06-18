/*
Copyright(c) 2016-2019 Panos Karabelas

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
//==========================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

Widget_Profiler::Widget_Profiler(Context* context) : Widget(context)
{
	m_window_flags |= ImGuiWindowFlags_AlwaysAutoResize;
	m_title							= "Profiler";
	m_isVisible						= false;
	m_profiler						= m_context->GetSubsystem<Profiler>().get();
	m_xMin							= 1000;
	m_yMin							= 715;
	m_xMax							= FLT_MAX;
	m_yMax							= FLT_MAX;	
	m_plot_times_cpu.resize(m_plot_size);
	m_plot_times_gpu.resize(m_plot_size);
}

void Widget_Profiler::Tick(float delta_time)
{
	if (!m_isVisible)
		return;

	static int item_type = 1;
	ImGui::RadioButton("CPU", &item_type, 0);
	ImGui::SameLine();
	ImGui::RadioButton("GPU", &item_type, 1);
	ImGui::SameLine();
	float interval = m_profiler->GetUpdateInterval();
	ImGui::DragFloat("Update interval (The smaller the interval the higher the performance impact)", &interval, 0.001f, 0.0f, 0.5f);
	m_profiler->SetUpdateInterval(interval);
	ImGui::Separator();
	bool show_cpu = (item_type == 0);

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
	const auto& time_blocks		= m_profiler->GetTimeBlocks();
	const auto time_block_count = static_cast<unsigned int>(time_blocks.size());
	const auto time_cpu			= m_profiler->GetTimeCpu();	
	const auto height			= ImGui::GetCurrentContext()->FontSize;
	const auto padding_x		= ImGui::GetStyle().WindowPadding.x;
	const auto spacing_y		= ImGui::GetStyle().FramePadding.y;
	const auto& color			= ImGui::GetStyle().Colors[ImGuiCol_FrameBgActive];
	auto pos					= ImGui::GetCursorScreenPos();

	// Time blocks	
	for (unsigned int i = 0; i < time_block_count; i++)
	{
		auto& time_block = time_blocks[i];

		if (!time_block.IsProfilingCpu())
			continue;

		const auto name		= string(time_block.GetTreeDepth(), '+') + time_block.GetName();
		const auto duration	= time_block.GetDurationCpu();
		const auto fraction	= duration / time_cpu;
		const auto width	= fraction * ImGui::GetWindowContentRegionWidth();

		// Draw
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + width, pos.y + height), IM_COL32(color.x * 255, color.y * 255, color.z * 255, 255));
		ImGui::Text("%s - %.2f ms", name.c_str(), duration);

		// New line
		if (i < time_block_count - 1)
		{
			pos.y += height + spacing_y;
		}
	}
	ImGui::SetCursorScreenPos(pos);

	ImGui::Separator();
	ShowPlot(m_plot_times_cpu, m_metric_cpu, !m_profiler->HasNewData() ? -1.0f : time_cpu);
}

void Widget_Profiler::ShowGPU()
{
	// Get stuff
	const auto& time_blocks		= m_profiler->GetTimeBlocks();
	const auto time_block_count	= static_cast<unsigned int>(time_blocks.size());
	const auto time_gpu			= m_profiler->GetTimeGpu();
	const auto time_frame		= m_profiler->GetTimeFrame();
	const auto height			= ImGui::GetCurrentContext()->FontSize;
	const auto padding_x		= ImGui::GetStyle().WindowPadding.x;
	const auto spacing_y		= ImGui::GetStyle().FramePadding.y;
	const auto& color			= ImGui::GetStyle().Colors[ImGuiCol_FrameBgActive];
	auto pos					= ImGui::GetCursorScreenPos();

	// Time blocks	
	for (unsigned int i = 0; i < time_block_count; i++)
	{
		auto& time_block = time_blocks[i];

		if (!time_block.IsProfilingGpu())
			continue;

		const auto name			= string(time_block.GetTreeDepth(), '+') + time_block.GetName();
		const auto duration		= time_block.GetDurationGpu();
		const auto fraction		= duration / time_gpu;
		const auto width		= fraction * ImGui::GetWindowContentRegionWidth();
		
		// Draw
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + width, pos.y + height), IM_COL32(color.x * 255, color.y * 255, color.z * 255, 255));
		ImGui::Text("%s - %.2f ms", name.c_str(), duration);

		// New line
		if (i < time_block_count - 1)
		{
			pos.y += height + spacing_y;
		}
	}
	ImGui::SetCursorScreenPos(pos);

	// Plot
	ImGui::Separator();
	ShowPlot(m_plot_times_gpu, m_metric_gpu, !m_profiler->HasNewData() ? -1.0f : time_gpu );

	// VRAM	
	ImGui::Separator();
	unsigned int memory_used		= m_profiler->GpuGetMemoryUsed();
	unsigned int memory_available	= m_profiler->GpuGetMemoryAvailable();
	string overlay					= "Memory " + to_string(memory_used) + "/" + to_string(memory_available) + " MB";
	ImGui::ProgressBar((float)memory_used / (float)memory_available, ImVec2(-1, 0), overlay.c_str());
}

void Widget_Profiler::ShowPlot(vector<float>& data, Metric& metric, float time_value)
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

	// Plot data
	if (ImGui::Button("Clear")) { metric.Clear(); }
	ImGui::SameLine();  ImGui::Text("Avg:%.2f, Min:%.2f, Max:%.2f", metric.m_avg, metric.m_min, metric.m_max);
	ImGui::PlotLines("", data.data(), static_cast<int>(data.size()), 0, "", metric.m_min, metric.m_max, ImVec2(ImGui::GetWindowContentRegionWidth(), 80));
}
