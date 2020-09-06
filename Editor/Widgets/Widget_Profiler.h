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
    Metric() { Clear(); }

    void AddSample(const float sample)
    {
        m_min = Spartan::Math::Helper::Min(m_min, sample);
        m_max = Spartan::Math::Helper::Max(m_max, sample);
        m_sum += sample;
        m_sample_count++;
        m_avg = float(m_sum / static_cast<float>(m_sample_count));
    }

    void Clear()
    {
        m_min            = FLT_MAX;
        m_max            = FLT_MIN;
        m_avg            = 0.0f;
        m_sum            = 0.0f;
        m_sample_count    = 0;
    }

    float m_min;
    float m_max;
    float m_avg;
    double m_sum;
    uint64_t m_sample_count;
};

class Widget_Profiler : public Widget
{
public:
    Widget_Profiler(Editor* editor);
    void Tick() override;

private:
    void ShowCPU();
    void ShowGPU();
    void ShowTimeBlock(const Spartan::TimeBlock& time_block, float total_time) const;
    void ShowPlot(std::vector<float>& data, Metric& metric, float time_value, bool is_stuttering) const;

    std::vector<float> m_plot_times_cpu;
    std::vector<float> m_plot_times_gpu;
    unsigned int m_plot_size = 400;
    Metric m_metric_cpu;
    Metric m_metric_gpu;
    Spartan::Profiler* m_profiler;
    float m_tree_depth_stride = 10;
};
