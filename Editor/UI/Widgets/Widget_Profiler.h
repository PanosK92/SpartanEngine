/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ========================
#include "Widget.h"
#include "..\..\ImGui\Source\imgui.h"
#include "Profiling\Profiler.h"
#include "Math\MathHelper.h"
#include "Core\Timer.h"
//===================================

struct Metric
{
	Metric()
	{
		m_min			= FLT_MAX;
		m_max			= FLT_MIN;
		m_avg			= 0.0f;
		m_sum			= 0.0f;
		m_sampleCount	= 0;
	}

	void AddSample(float sample)
	{
		m_min = Directus::Math::Min(m_min, sample);
		m_max = Directus::Math::Max(m_max, sample);	
		m_sum += sample;
		m_sampleCount++;
		m_avg = float(m_sum / (float)m_sampleCount);
	}

	float m_min;
	float m_max;
	float m_avg;
	double m_sum;
	uint64_t m_sampleCount;
};

class Widget_Profiler : public Widget
{
public:
	Widget_Profiler(){}

	void Initialize(Directus::Context* context) override
	{
		Widget::Initialize(context);

		m_title					= "Profiler";
		m_updateFrequency		= 0.05f;
		m_timeSinceLastUpdate	= m_updateFrequency;
		m_xMin					= 1000;
		m_yMin					= 715;
		m_xMax					= FLT_MAX;
		m_yMax					= FLT_MAX;
		// Fill with dummy values so that the plot can progress immediately
		if (m_cpuTimes.empty() && m_gpuTimes.empty())
		{
			m_cpuTimes.resize(200);
			m_gpuTimes.resize(200);
		}
	}

	void Update(float deltaTime) override
	{
		if (!m_isVisible)
			return;

		Widget::Begin();

		// Get some useful things
		auto& cpuBlocks			= Directus::Profiler::Get().GetTimeBlocks_CPU();
		auto gpuBlocks			= Directus::Profiler::Get().GetTimeBlocks_GPU();
		float renderTimeCPU		= Directus::Profiler::Get().GetRenderTime_CPU();
		float renderTimeGPU		= Directus::Profiler::Get().GetRenderTime_GPU();
		float renderTimeTotal	= Directus::Profiler::Get().GetRenderTime_CPU() + Directus::Profiler::Get().GetRenderTime_GPU();

		// Milliseconds
		{
			ImGui::Columns(3, "##Widget_Profiler");
			ImGui::Text("Function");		ImGui::NextColumn();
			ImGui::Text("Duration (CPU)");	ImGui::NextColumn();
			ImGui::Text("Duration (GPU)");	ImGui::NextColumn();
			ImGui::Separator();

			for (const auto& cpuBlock : cpuBlocks)
			{
				ImGui::Text("%s", cpuBlock.first);				ImGui::NextColumn();
				// CPU entry
				ImGui::Text("%f ms", cpuBlock.second.duration);	ImGui::NextColumn();
				// GPU entry
				bool exists = gpuBlocks.find(cpuBlock.first) != gpuBlocks.end();
				exists ? ImGui::Text("%f ms", gpuBlocks[cpuBlock.first].duration) : ImGui::Text("N/A"); ImGui::NextColumn();
			}
			ImGui::Columns(1);
		}

		ImGui::Separator();

		// Plots
		{
			// Get sample data
			m_timeSinceLastUpdate += deltaTime;
			if (m_timeSinceLastUpdate >= m_updateFrequency)
			{
				float cpuMs = m_context->GetSubsystem<Directus::Timer>()->GetDeltaTimeMs();
				m_metric_cpu.AddSample(cpuMs);
				m_cpuTimes.emplace_back(cpuMs);
				if (m_cpuTimes.size() >= 200)
				{
					m_cpuTimes.erase(m_cpuTimes.begin());
				}

				m_metric_gpu.AddSample(renderTimeGPU);
				m_gpuTimes.emplace_back(renderTimeGPU);

				if (m_gpuTimes.size() >= 200)
				{
					m_gpuTimes.erase(m_gpuTimes.begin());
				}
				m_timeSinceLastUpdate = 0.0f;
			}

			ImGui::Text("CPU: Avg:%.2f, Min:%.2f, Max:%.2f", m_metric_cpu.m_avg, m_metric_cpu.m_min, m_metric_cpu.m_max);
			ImGui::PlotLines("", m_cpuTimes.data(), (int)m_cpuTimes.size(), 0, "", m_metric_cpu.m_min, m_metric_cpu.m_max, ImVec2(ImGui::GetWindowContentRegionWidth(), 80));
			ImGui::Separator();
			ImGui::Text("GPU: Avg:%.2f, Min:%.2f, Max:%.2f", m_metric_gpu.m_avg, m_metric_gpu.m_min, m_metric_gpu.m_max);
			ImGui::PlotLines("", m_gpuTimes.data(), (int)m_gpuTimes.size(), 0, "", m_metric_gpu.m_min, m_metric_gpu.m_max, ImVec2(ImGui::GetWindowContentRegionWidth(), 80));
		}

		// Bars
		{
			ImGui::Separator();
			ImGui::Text("GPU Workload Distribution");
			ImVec2 pos		= ImGui::GetCursorScreenPos();
			ImVec2 a		= pos;
			ImVec2 b		= pos;	
			float penX		= pos.x;
			float penY		= pos.y;
			float paddingX	= ImGui::GetStyle().WindowPadding.x;
			float spacingY	= ImGui::GetStyle().FramePadding.y;
			float widthMin	= 10.0f;
			float height	= 20.0f;
			for (const auto& gpuBlock : gpuBlocks)
			{
				// Ignore main render block (we are displaying it's contents in relation to each other here)
				if (strcmp(gpuBlock.first, "Directus::Renderer::Render") == 0) // must not compare str
					continue;

				float duration = gpuBlock.second.duration;

				float width = ImGui::GetWindowContentRegionWidth() / (renderTimeGPU / duration);
				width = Directus::Math::Max(width, widthMin);
				Directus::Math::Vector3 color = Directus::Math::Lerp(Directus::Math::Vector3(0.0f, 200.0f, 0.0f), Directus::Math::Vector3(255.0f, 0.0f, 0.0f), (duration / renderTimeGPU));
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(penX, penY), ImVec2(penX + width, penY + 20.0f), IM_COL32(color.x, color.y, 0, 255));
				ImGui::GetWindowDrawList()->AddText(ImVec2(penX + paddingX, penY + 2.0f), IM_COL32(255, 255, 255, 255), gpuBlock.first);
				penY += height + spacingY;
			}

			float cpuWidth = (renderTimeCPU / renderTimeTotal) * ImGui::GetWindowContentRegionWidth();
			float gpuWidth = (renderTimeGPU / renderTimeTotal) * ImGui::GetWindowContentRegionWidth();
			float splitX = penX + cpuWidth;
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(penX, penY), ImVec2(splitX, penY + 20.0f), IM_COL32(0, 150, 255, 255));		
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(splitX, penY), ImVec2(splitX + gpuWidth, penY + 20.0f), IM_COL32(120, 200, 120, 255));
			ImGui::GetWindowDrawList()->AddText(ImVec2(penX + paddingX, penY + 2.0f), IM_COL32(255, 255, 255, 255), std::string("CPU " + std::to_string((renderTimeCPU / renderTimeTotal) * 100.0f) + "%").c_str());
			ImGui::GetWindowDrawList()->AddText(ImVec2(splitX + gpuWidth - 120.0f, penY + 2.0f), IM_COL32(255, 255, 255, 255), std::string("GPU " + std::to_string((renderTimeGPU / renderTimeTotal) * 100.0f) + "%").c_str());

			ImGui::SetCursorPosY(penY - 160);
		}

		Widget::End();
	}

private:
	std::vector<float> m_cpuTimes;
	std::vector<float> m_gpuTimes;
	float m_updateFrequency;
	float m_timeSinceLastUpdate;
	Metric m_metric_cpu;
	Metric m_metric_gpu;
};
