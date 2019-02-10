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

//= INCLUDES ==================
#include "Widget.h"
#include "Profiling/Profiler.h"
#include "Math/MathHelper.h"
#include "Core/Timer.h"
#include <vector>
//=============================

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
		m_min = Directus::Math::Helper::Min(m_min, sample);
		m_max = Directus::Math::Helper::Max(m_max, sample);
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
	Widget_Profiler(Directus::Context* context);
	void Tick(float deltaTime) override;

private:
	std::vector<float> m_cpuTimes;
	std::vector<float> m_gpuTimes;
	float m_updateFrequency;
	float m_plotTimeSinceLastUpdate;
	Metric m_metric_cpu;
	Metric m_metric_gpu;
	Directus::Profiler* m_profiler;
};
