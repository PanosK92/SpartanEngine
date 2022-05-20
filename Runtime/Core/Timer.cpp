/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "Spartan.h"
#include "../Display/Display.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    Timer::Timer(Context* context) : Subsystem(context)
    {
        m_time_start     = chrono::high_resolution_clock::now();
        m_time_sleep_end = chrono::high_resolution_clock::now();
    }

    void Timer::OnTick(double _delta_time)
    {
        // Compute delta time
        m_time_sleep_start = chrono::high_resolution_clock::now();
        chrono::duration<double, milli> delta_time = m_time_sleep_start - m_time_sleep_end;

        // FPS limiting
        {
            // The kernel takes time to wake up the thread after the thread has finished sleeping.
            // It can't be trusted for accurate frame limiting, hence we do it simple stupid.
            double target_ms = 1000.0 / m_fps_limit;
            while (delta_time.count() < target_ms)
            {
                delta_time = chrono::high_resolution_clock::now() - m_time_sleep_start;
            }

            m_time_sleep_end = chrono::high_resolution_clock::now();
        }

        // Compute durations
        m_delta_time_ms = static_cast<double>(delta_time.count());
        m_time_ms       = static_cast<double>(chrono::duration<double, milli>(m_time_sleep_start - m_time_start).count());

        // Compute smoothed delta time
        const double frames_to_accumulate = 10;
        const double delta_feedback       = 1.0 / frames_to_accumulate;
        m_delta_time_smoothed_ms          = m_delta_time_smoothed_ms * (1.0 - delta_feedback) + m_delta_time_ms * delta_feedback;
    }

    void Timer::SetFpsLimit(double fps_in)
    {
        if (fps_in < 0.0f) // negative -> match monitor's refresh rate
        {
            const DisplayMode& display_mode = Display::GetActiveDisplayMode();
            fps_in = display_mode.hz;
        }

        // Clamp to a minimum of 10 FPS to avoid unresponsiveness
        fps_in = Math::Helper::Clamp(fps_in, m_fps_min, m_fps_max);

        if (m_fps_limit == fps_in)
            return;

        m_user_selected_fps_target = true;
        m_fps_limit = fps_in;
        LOG_INFO("Set to %.2f FPS", m_fps_limit);
    }

    FpsLimitType Timer::GetFpsLimitType()
    {
        if (m_fps_limit == Display::GetActiveDisplayMode().hz)
            return FpsLimitType::FixedToMonitor;

        if (m_fps_limit == m_fps_max)
            return FpsLimitType::Unlocked;

        return FpsLimitType::Fixed;
    }
}
