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

#pragma once

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

Widget_Profiler::Widget_Profiler(Directus::Context* context) : Widget(context)
{
	m_title						= "Profiler";
	m_isVisible					= false;
	m_updateFrequency			= 0.05f;
	m_plotTimeSinceLastUpdate	= m_updateFrequency;
	m_xMin						= 1000;
	m_yMin						= 715;
	m_xMax						= FLT_MAX;
	m_yMax						= FLT_MAX;
	m_profiler					= m_context->GetSubsystem<Profiler>().get();

	// Fill with dummy values so that the plot can progress immediately
	if (m_cpuTimes.empty() && m_gpuTimes.empty())
	{
		m_cpuTimes.resize(200);
		m_gpuTimes.resize(200);
	}
}

void Widget_Profiler::Tick(float deltaTime)
{
	// Only profile when user is observing (because it can be expensive)
	m_profiler->SetProfilingEnabled_CPU(m_isVisible);
	m_profiler->SetProfilingEnabled_GPU(m_isVisible);

	if (!m_isVisible)
		return;

	// Get CPU & GPU timings
	auto& cpuBlocks			= m_profiler->GetTimeBlocks_CPU();
	auto gpuBlocks			= m_profiler->GetTimeBlocks_GPU();
	float renderTimeCPU		= m_profiler->GetRenderTime_CPU();
	float renderTimeGPU		= m_profiler->GetRenderTime_GPU();
	float renderTimeTotal	= m_profiler->GetRenderTime_CPU() + m_profiler->GetRenderTime_GPU();

	m_plotTimeSinceLastUpdate	+= deltaTime;
	bool plot_update			= m_plotTimeSinceLastUpdate >= m_updateFrequency;
	m_plotTimeSinceLastUpdate	= plot_update ? 0.0f : m_plotTimeSinceLastUpdate;

	ImGui::Text("CPU");
	{
		// Functions
		for (const auto& cpuBlock : cpuBlocks)
		{
			ImGui::Text("%s - %f ms", cpuBlock.first, cpuBlock.second.duration);
		}

		// Plot
		if (plot_update)
		{
			m_metric_cpu.AddSample(renderTimeCPU);
			m_cpuTimes.emplace_back(renderTimeCPU);
			if (m_cpuTimes.size() >= 200)
			{
				m_cpuTimes.erase(m_cpuTimes.begin());
			}
		}
		ImGui::Text("Avg:%.2f, Min:%.2f, Max:%.2f", m_metric_cpu.m_avg, m_metric_cpu.m_min, m_metric_cpu.m_max);
		ImGui::PlotLines("", m_cpuTimes.data(), (int)m_cpuTimes.size(), 0, "", m_metric_cpu.m_min, m_metric_cpu.m_max, ImVec2(ImGui::GetWindowContentRegionWidth(), 80));
	}

	ImGui::Separator();

	ImGui::Text("GPU");
	{
		// Functions		
		ImVec2 pos			= ImGui::GetCursorScreenPos();
		float height		= 20.0f;
		float paddingX		= ImGui::GetStyle().WindowPadding.x;
		float spacingY		= ImGui::GetStyle().FramePadding.y;
		ImGuiStyle& style	= ImGui::GetStyle();
		for (const auto& gpuBlock : gpuBlocks)
		{
			string name		= gpuBlock.first;
			float duration	= gpuBlock.second.duration;
			float fraction	= duration / renderTimeGPU;
			float width		= fraction * ImGui::GetWindowContentRegionWidth();
			auto color		= style.Colors[ImGuiCol_FrameBgActive];

			// Draw
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + width, pos.y + height), IM_COL32(color.x * 255, color.y * 255, color.z * 255, 255));
			ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + paddingX, pos.y + 2.0f), IM_COL32(255, 255, 255, 255), (name + " - " + to_string(duration) + " ms").c_str());

			// New line
			pos.y += height + spacingY;
		}
		ImGui::SetCursorScreenPos(pos);

		// Plot
		if (plot_update)
		{
			m_metric_gpu.AddSample(renderTimeGPU);
			m_gpuTimes.emplace_back(renderTimeGPU);

			if (m_gpuTimes.size() >= 200)
			{
				m_gpuTimes.erase(m_gpuTimes.begin());
			}
		}
		ImGui::Text("Avg:%.2f, Min:%.2f, Max:%.2f", m_metric_gpu.m_avg, m_metric_gpu.m_min, m_metric_gpu.m_max);
		ImGui::PlotLines("", m_gpuTimes.data(), (int)m_gpuTimes.size(), 0, "", m_metric_gpu.m_min, m_metric_gpu.m_max, ImVec2(ImGui::GetWindowContentRegionWidth(), 80));
	}
}
