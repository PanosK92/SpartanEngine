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

//= INCLUDES ==================
#include "Timer.h"
#include "Settings.h"
#include "../Logging/Log.h"
#include "../Math/MathHelper.h"
//=============================

//= NAMESPACES ========
using namespace std;
using namespace chrono;
//=====================

namespace Spartan
{
	Timer::Timer(Context* context) : ISubsystem(context)
	{
		time_a  = high_resolution_clock::now();
		time_b  = high_resolution_clock::now();
	}

	void Timer::Tick(float delta_time)
	{
		// Compute work time
		time_a								= high_resolution_clock::now();
		duration<double, milli> time_work	= time_a - time_b;
		
		// Compute sleep time
		const double max_fps		= m_fps_target;
		const double min_dt			= 1000.0 / max_fps;
		const double dt_remaining	= min_dt - time_work.count();

        // Sleep - fps limiting
		if (dt_remaining > 0)
		{
			this_thread::sleep_for(milliseconds(static_cast<int64_t>(dt_remaining)));
		}

		// Compute delta time
		time_b										= high_resolution_clock::now();
		const duration<double, milli> time_sleep	= time_b - time_a;
		m_delta_time_ms								= (time_work + time_sleep).count();

        // Compute smoothed delta time
        double frames_to_accumulate = 5;
        double delta_feedback       = 1.0 / frames_to_accumulate;
        double delta_max            = 1000.0 / m_fps_min;
        double delta_clamped        = m_delta_time_ms > delta_max ? delta_max : m_delta_time_ms; // If frame time is too high/slow, clamp it   
        m_delta_time_smoothed_ms    = m_delta_time_smoothed_ms * (1.0 - delta_feedback) + delta_clamped * delta_feedback;
	}

    void Timer::SetTargetFps(double fps)
    {
        if (fps < 0.0f) // negative -> match monitor's refresh rate
        {
            m_fps_policy    = Fps_FixedMonitor;
            fps             = m_monitor_refresh_rate; 
        }
        else if (fps >= 0.0f && fps < 10.0f) // zero or very small -> unlock
        {
            m_fps_policy    = Fps_Unlocked;
            fps             = m_fps_max;
        }
        else
        {
            m_fps_policy = Fps_Fixed;
        }

        if (m_fps_target == fps)
            return;

        m_fps_target = fps;
        m_user_selected_fps_target = true;
        LOGF_INFO("FPS limit set to %.2f", m_fps_target);
    }

    void Timer::AddMonitorRefreshRate(double refresh_rate)
    {
        refresh_rate = Math::Max(m_monitor_refresh_rate, refresh_rate);

        if (m_monitor_refresh_rate == refresh_rate)
            return;

        m_monitor_refresh_rate = refresh_rate;

        // If the user hasn't specified an fps target, try to match the monitor
        if (!m_user_selected_fps_target)
        {
            m_fps_target = m_monitor_refresh_rate;
        }

        LOGF_INFO("Maximum monitor refresh rate set to %.2f hz", m_monitor_refresh_rate);
    }
}
