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

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
using namespace Helper;
//=======================

Widget_Profiler::Widget_Profiler(Context* context) : Widget(context)
{
	m_title							= "Profiler";
	m_isVisible						= false;
	m_update_frequency				= 0.05f;
	m_plot_time_since_last_update	= m_update_frequency;
	m_xMin							= 1000;
	m_yMin							= 715;
	m_xMax							= FLT_MAX;
	m_yMax							= FLT_MAX;
	m_profiler						= m_context->GetSubsystem<Profiler>().get();

	// Fill with dummy values so that the plot can progress immediately
	if (m_cpu_times.empty() && m_gpu_times.empty())
	{
		m_cpu_times.resize(200);
		m_gpu_times.resize(200);
	}
}

void Widget_Profiler::Tick(float delta_time)
{
	if (!m_isVisible)
		return;

	// Get CPU & GPU timings
	auto& time_blocks	= m_profiler->GetTimeBlocks();
	auto time_cpu		= m_profiler->GetTimeCpu();
	auto time_gpu		= m_profiler->GetTimeGpu();
	auto time_frame		= m_profiler->GetTimeFrame();

	m_plot_time_since_last_update	+= delta_time;
	const auto plot_update			= m_plot_time_since_last_update >= m_update_frequency;
	m_plot_time_since_last_update	= plot_update ? 0.0f : m_plot_time_since_last_update;

	ImGui::Text("CPU");
	{
		// Functions
		for (const auto& time_block : time_blocks)
		{
			if (!time_block.IsProfilingCpu())
				continue;

			ImGui::Text("%s - %f ms", time_block.GetName().c_str(), time_block.GetDurationCpu());
		}

		// Plot
		if (plot_update)
		{
			m_metric_cpu.AddSample(time_cpu);
			m_cpu_times.emplace_back(time_cpu);
			if (m_cpu_times.size() >= 200)
			{
				m_cpu_times.erase(m_cpu_times.begin());
			}
		}
		ImGui::Text("Avg:%.2f, Min:%.2f, Max:%.2f", m_metric_cpu.m_avg, m_metric_cpu.m_min, m_metric_cpu.m_max);
		ImGui::PlotLines("", m_cpu_times.data(), static_cast<int>(m_cpu_times.size()), 0, "", m_metric_cpu.m_min, m_metric_cpu.m_max, ImVec2(ImGui::GetWindowContentRegionWidth(), 80));
	}

	ImGui::Separator();

	ImGui::Text("GPU");
	{
		// Functions		
		auto pos				= ImGui::GetCursorScreenPos();
		const auto height		= 20.0f;
		const auto padding_x	= ImGui::GetStyle().WindowPadding.x;
		const auto spacing_y	= ImGui::GetStyle().FramePadding.y;
		auto& style		= ImGui::GetStyle();
		for (const auto& time_block : time_blocks)
		{
			if (!time_block.IsProfilingGpu())
				continue;

			const auto& name	= time_block.GetName();
			const auto duration	= time_block.GetDurationGpu();
			const auto fraction	= duration / time_gpu;
			const auto width	= fraction * ImGui::GetWindowContentRegionWidth();
			const auto color	= style.Colors[ImGuiCol_FrameBgActive];

			// Draw
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + width, pos.y + height), IM_COL32(color.x * 255, color.y * 255, color.z * 255, 255));
			ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + padding_x, pos.y + 2.0f), IM_COL32(255, 255, 255, 255), (name + " - " + to_string(duration) + " ms").c_str());

			// New line
			pos.y += height + spacing_y;
		}
		ImGui::SetCursorScreenPos(pos);

		// Plot
		if (plot_update)
		{
			m_metric_gpu.AddSample(time_gpu);
			m_gpu_times.emplace_back(time_gpu);

			if (m_gpu_times.size() >= 200)
			{
				m_gpu_times.erase(m_gpu_times.begin());
			}
		}
		ImGui::Text("Avg:%.2f, Min:%.2f, Max:%.2f", m_metric_gpu.m_avg, m_metric_gpu.m_min, m_metric_gpu.m_max);
		ImGui::PlotLines("", m_gpu_times.data(), static_cast<int>(m_gpu_times.size()), 0, "", m_metric_gpu.m_min, m_metric_gpu.m_max, ImVec2(ImGui::GetWindowContentRegionWidth(), 80));
	}
}
