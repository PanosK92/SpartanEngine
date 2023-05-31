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

//= INCLUDES ==================
#include "pch.h"
#include "../Display/Display.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    namespace
    {
        static const uint32_t frames_to_accumulate = 20;
        static const double weight_delta           = 1.0 / static_cast<float>(frames_to_accumulate);

        // Frame time
        static chrono::high_resolution_clock::time_point m_time_start;
        static chrono::high_resolution_clock::time_point m_time_sleep_start;
        static chrono::high_resolution_clock::time_point m_time_sleep_end;
        static double m_time_ms                = 0.0f;
        static double m_delta_time_ms          = 0.0f;
        static double m_delta_time_smoothed_ms = 0.0f;
        static double m_sleep_overhead         = 0.0f;

        // FPS
        static float m_fps_min                 = 10.0f;
        static float m_fps_max                 = 5000.0f;
        static float m_fps_limit               = m_fps_min; // if it's lower than the monitor's hz, it will be updated to match it, so start with something low.
        static float m_fps_limit_previous      = m_fps_limit;
        static bool m_user_selected_fps_target = false;
    }

    void Timer::Initialize()
    {
        m_time_start     = chrono::high_resolution_clock::now();
        m_time_sleep_end = chrono::high_resolution_clock::now();
    }

    void Timer::Tick()
    {
        // Compute delta time
        m_time_sleep_start = chrono::steady_clock::now();
        chrono::duration<double, milli> delta_time = m_time_sleep_start - m_time_sleep_end;

        // FPS limiting
        {
            double target_ms = 1000.0 / m_fps_limit;
            while (delta_time.count() < target_ms)
            {
                // Yield the CPU to other threads/processes instead of busy waiting.
                std::this_thread::yield();

                delta_time = chrono::steady_clock::now() - m_time_sleep_start;
            }

            m_time_sleep_end = chrono::steady_clock::now();
        }

        // Compute times
        m_delta_time_ms          = static_cast<double>(delta_time.count());
        m_time_ms                = static_cast<double>(chrono::duration<double, milli>(m_time_sleep_start - m_time_start).count());
        m_delta_time_smoothed_ms = m_delta_time_smoothed_ms * (1.0 - weight_delta) + m_delta_time_ms * weight_delta;
    }

    void Timer::SetFpsLimit(float fps_in)
    {
        if (fps_in < 0.0f) // negative -> match monitor's refresh rate
        {
            fps_in = static_cast<float>(Display::GetRefreshRate());
        }

        // Clamp to a minimum of 10 FPS to avoid unresponsiveness
        fps_in = Math::Helper::Clamp(fps_in, m_fps_min, m_fps_max);

        if (m_fps_limit == fps_in)
            return;

        m_user_selected_fps_target = true;
        m_fps_limit = fps_in;
        SP_LOG_INFO("Set to %.2f FPS", m_fps_limit);
    }

    float Timer::GetFpsLimit()
    {
        return m_fps_limit;
    }

    FpsLimitType Timer::GetFpsLimitType()
    {
        if (m_fps_limit == static_cast<float>(Display::GetRefreshRate()))
            return FpsLimitType::FixedToMonitor;

        if (m_fps_limit == m_fps_max)
            return FpsLimitType::Unlocked;

        return FpsLimitType::Fixed;
    }

    void Timer::OnVsyncToggled(const bool enabled)
    {
        if (enabled)
        {
            m_fps_limit_previous = m_fps_limit;
            SetFpsLimit(static_cast<float>(Display::GetRefreshRate()));
        }
        else
        {
            SetFpsLimit(m_fps_limit_previous);
        }
    }

    double Timer::GetTimeMs()
    {
        return m_time_ms;
    }

    double Timer::GetTimeSec()
    {
        return m_time_ms / 1000.0;
    }

    double Timer::GetDeltaTimeMs()
    {
        return m_delta_time_ms;
    }

    double Timer::GetDeltaTimeSec()
    {
        return m_delta_time_ms / 1000.0;
    }

    double Timer::GetDeltaTimeSmoothedMs()
    {
        return m_delta_time_smoothed_ms;
    }

    double Timer::GetDeltaTimeSmoothedSec()
    {
        return m_delta_time_smoothed_ms / 1000.0;
    }
}
