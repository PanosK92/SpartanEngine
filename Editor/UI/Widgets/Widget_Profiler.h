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
#include <vector>
#include "..\..\ImGui\Source\imgui.h"
#include "Profiling\Profiler.h"
#include "Math\MathHelper.h"
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
		m_avg = m_sum / (float)m_sampleCount;
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
		m_xMin					= 720;
		m_yMin					= 400;
		m_xMax					= FLT_MAX;
		m_yMax					= FLT_MAX;
	}

	void Update(float deltaTime) override
	{
		if (!m_isVisible)
			return;

		Widget::Begin();

		ImGui::Columns(3, "##Widget_Profiler");
		ImGui::Text("Function");		ImGui::NextColumn();
		ImGui::Text("Duration (CPU)");	ImGui::NextColumn();
		ImGui::Text("Duration (GPU)");	ImGui::NextColumn();
		ImGui::Separator();

		auto& cpuBlocks = Directus::Profiler::Get().GetTimeBlocks_CPU();
		auto gpuBlocks	= Directus::Profiler::Get().GetTimeBlocks_GPU();

		for (const auto& cpuBlock : cpuBlocks)
		{
			auto& gpuBlock = gpuBlocks[cpuBlock.first];

			ImGui::Text("%s", cpuBlock.first);				ImGui::NextColumn();
			ImGui::Text("%f ms", cpuBlock.second.duration);	ImGui::NextColumn();
			gpuBlock.initialized ? ImGui::Text("%f ms", gpuBlock.duration) : ImGui::Text("N/A"); ImGui::NextColumn();
		}
		ImGui::Columns(1);

		ImGui::Separator();

		// If we are just starting, fill with dummy values so that the plot can progress immediately
		if (m_cpuTimes.empty() && m_gpuTimes.empty())
		{
			m_cpuTimes.resize(100);
			m_gpuTimes.resize(100);
		}

		// Get sample data
		m_timeSinceLastUpdate += deltaTime;
		if (m_timeSinceLastUpdate >= m_updateFrequency)
		{
			float cpuMs	= Directus::Profiler::Get().GetTimeCPU();
			m_metric_cpu.AddSample(cpuMs);
			m_cpuTimes.emplace_back(cpuMs);
			if (m_cpuTimes.size() >= 100)
			{
				m_cpuTimes.erase(m_cpuTimes.begin());
			}

			float gpuMs	= Directus::Profiler::Get().GetTimeGPU();
			m_metric_gpu.AddSample(gpuMs);
			m_gpuTimes.emplace_back(gpuMs);

			if (m_gpuTimes.size() >= 100)
			{
				m_gpuTimes.erase(m_gpuTimes.begin());
			}
			m_timeSinceLastUpdate = 0.0f;
		}

		ImGui::Text("CPU: Avg:%.2f, Min:%.2f, Max:%.2f", m_metric_cpu.m_avg, m_metric_cpu.m_min, m_metric_cpu.m_max);
		ImGui::PlotLines("", m_cpuTimes.data(), m_cpuTimes.size(), 0, "", m_metric_cpu.m_min, m_metric_cpu.m_max, ImVec2(ImGui::GetWindowContentRegionWidth(), 80));
		ImGui::Separator();
		ImGui::Text("GPU: Avg:%.2f, Min:%.2f, Max:%.2f", m_metric_gpu.m_avg, m_metric_gpu.m_min, m_metric_gpu.m_max);
		ImGui::PlotLines("", m_gpuTimes.data(), m_gpuTimes.size(), 0, "",  m_metric_gpu.m_min, m_metric_gpu.m_max, ImVec2(ImGui::GetWindowContentRegionWidth(), 80));

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
