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

//= INCLUDES ==================
#include "Spartan.h"
#include "../Display/Display.h"
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    Timer::Timer(Context* context) : ISubsystem(context)
    {
        m_time_start        = chrono::high_resolution_clock::now();
        m_time_frame_start  = chrono::high_resolution_clock::now();
        m_time_frame_end    = chrono::high_resolution_clock::now();
    }

    void Timer::Tick(float delta_time)
    {
        // Get time
        m_time_frame_end    = m_time_frame_start;
        m_time_frame_start  = chrono::high_resolution_clock::now();

        // Compute durations
        const chrono::duration<double, milli> time_elapsed      = m_time_start - m_time_frame_start;
        chrono::duration<double, milli> time_delta              = m_time_frame_start - m_time_frame_end;
        const chrono::duration<double, milli> time_remaining    = chrono::duration<double, milli>(1000.0 / m_fps_target) - time_delta;

        // Fps limiting
        if (time_remaining.count() > 0)
        {
            // Compute sleep duration and account for the sleep overhead.
            // The sleep overhead is the time the kernel takes to wake up the thread after the thread has finished sleeping.
            const chrono::duration<double, milli> sleep_duration_requested = chrono::duration<double, milli>(time_remaining.count() - m_sleep_overhead);

            // Put the thread to sleep
            this_thread::sleep_until(m_time_frame_start + sleep_duration_requested);

            // Compute sleep overhead (to use in next tick)
            const chrono::duration<double, milli> sleep_time_real = chrono::high_resolution_clock::now() - m_time_frame_start;
            m_sleep_overhead = sleep_time_real.count() - sleep_duration_requested.count();

            // Account for sleep duration
            time_delta += sleep_time_real;
        }

        // Save times
        m_time_ms           = static_cast<double>(time_elapsed.count());
        m_delta_time_ms     = static_cast<double>(time_delta.count());

        // Compute smoothed delta time
        const double frames_to_accumulate   = 5;
        const double delta_feedback         = 1.0 / frames_to_accumulate;
        double delta_max                    = 1000.0 / m_fps_min;
        const double delta_clamped          = m_delta_time_ms > delta_max ? delta_max : m_delta_time_ms; // If frame time is too high/slow, clamp it   
        m_delta_time_smoothed_ms            = m_delta_time_smoothed_ms * (1.0 - delta_feedback) + delta_clamped * delta_feedback;
    }

    void Timer::SetTargetFps(double fps_in)
    {
        if (fps_in < 0.0f) // negative -> match monitor's refresh rate
        {
            m_fps_policy = Fps_FixedMonitor;
            const DisplayMode& display_mode = Display::GetActiveDisplayMode();
            fps_in = display_mode.hz;
        }
        else if (fps_in >= 0.0f && fps_in < 10.0f) // zero or very small -> unlock to avoid unresponsiveness
        {
            m_fps_policy    = Fps_Unlocked;
            fps_in          = m_fps_max;
        }
        else // anything decent, let it happen
        {
            m_fps_policy = Fps_Fixed;
        }

        if (m_fps_target == fps_in)
            return;

        m_fps_target = fps_in;
        m_user_selected_fps_target = true;
        LOG_INFO("Set to %.2f FPS", m_fps_target);
    }
}
