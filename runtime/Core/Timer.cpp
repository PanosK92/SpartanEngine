/*
Copyright(c) 2016-2024 Panos Karabelas

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
        // accumulation
        const uint32_t frames_to_accumulate = 15;
        const double weight_delta           = 1.0 / static_cast<float>(frames_to_accumulate);

        // frame time
        double time_ms                = 0.0f;
        double delta_time_ms          = 0.0f;
        double delta_time_smoothed_ms = 0.0f;

        // fps
        float fps_min            = 30.0f;
        float fps_max            = 10000.0f;
        float fps_limit          = fps_min;
        float fps_limit_previous = fps_limit;

        // misc
        chrono::steady_clock::time_point last_tick_time;
    }

    void Timer::Initialize()
    {
        fps_limit      = static_cast<float>(Display::GetRefreshRate());
        last_tick_time = chrono::steady_clock::now();
    }

    void Timer::PostTick()
    {
        // if this is not the first tick, we calculate the delta time
        if (last_tick_time.time_since_epoch() != chrono::steady_clock::duration::zero())
        {
            delta_time_ms = static_cast<double>(chrono::duration<double, milli>(chrono::steady_clock::now() - last_tick_time).count());
        }

        // fps limit
        double target_ms = 1000.0 / fps_limit;
        while (delta_time_ms < target_ms)
        {
            delta_time_ms = static_cast<double>(chrono::duration<double, milli>(chrono::steady_clock::now() - last_tick_time).count());
        }

        // compute delta time based timings
        delta_time_smoothed_ms  = delta_time_smoothed_ms * (1.0 - weight_delta) + delta_time_ms * weight_delta;
        time_ms                += delta_time_ms;

        // end
        last_tick_time = chrono::steady_clock::now();
    }

    void Timer::SetFpsLimit(float fps_in)
    {
        if (fps_in < 0.0f) // negative -> match monitor's refresh rate
        {
            fps_in = static_cast<float>(Display::GetRefreshRate());
        }

        // clamp to a minimum of 10 FPS to avoid unresponsiveness
        fps_in = Math::Helper::Clamp(fps_in, fps_min, fps_max);

        if (fps_limit == fps_in)
            return;

        fps_limit = fps_in;
        SP_LOG_INFO("Set to %.2f FPS", fps_limit);
    }

    float Timer::GetFpsLimit()
    {
        return fps_limit;
    }

    FpsLimitType Timer::GetFpsLimitType()
    {
        if (fps_limit == static_cast<float>(Display::GetRefreshRate()))
            return FpsLimitType::FixedToMonitor;

        if (fps_limit == fps_max)
            return FpsLimitType::Unlocked;

        return FpsLimitType::Fixed;
    }

    void Timer::OnVsyncToggled(const bool enabled)
    {
        if (enabled)
        {
            fps_limit_previous = fps_limit;
            SetFpsLimit(static_cast<float>(Display::GetRefreshRate()));
        }
        else
        {
            SetFpsLimit(fps_limit_previous);
        }
    }

    double Timer::GetTimeMs()
    {
        return time_ms;
    }

    double Timer::GetTimeSec()
    {
        return time_ms / 1000.0;
    }

    double Timer::GetDeltaTimeMs()
    {
        return delta_time_ms;
    }

    double Timer::GetDeltaTimeSec()
    {
        return delta_time_ms / 1000.0;
    }

    double Timer::GetDeltaTimeSmoothedMs()
    {
        return delta_time_smoothed_ms;
    }

    double Timer::GetDeltaTimeSmoothedSec()
    {
        return delta_time_smoothed_ms / 1000.0;
    }
}
