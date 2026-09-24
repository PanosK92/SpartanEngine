/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================
#include "pch.h"
#include "../display/Display.h"
#include <SDL3/SDL_timer.h>
#include <cmath>
//=============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        // accumulation
        const uint32_t frames_to_accumulate = 15;
        const double weight_delta           = 1.0 / static_cast<double>(frames_to_accumulate);

        // frame time
        double time_ms                = 0.0;
        double delta_time_ms          = 0.0;
        double delta_time_smoothed_ms = 0.0;
        double pacing_time_ms         = 0.0;

        // fps
        constexpr float fps_min  = 30.0f;
        constexpr float fps_max  = 10000.0f;
        float fps_limit          = fps_min;
        float fps_limit_previous = fps_limit;

        // use one monotonic clock for deadlines, deltas and elapsed time
        using Clock = chrono::steady_clock;
        Clock::time_point start_time;
        Clock::time_point last_tick_time;
        Clock::time_point next_tick_time;
        bool reset_deadline = true;
        bool has_sample     = false;
        bool vsync_enabled  = false;

        float get_monitor_fps()
        {
            const float refresh_rate = Display::GetRefreshRate();
            return isfinite(refresh_rate) && refresh_rate > 0.0f ? clamp(refresh_rate, fps_min, fps_max) : 60.0f;
        }
    }

    void Timer::Initialize()
    {
        fps_limit          = get_monitor_fps();
        fps_limit_previous = fps_limit;
        vsync_enabled      = false;
        Reset();
    }

    void Timer::Reset()
    {
        start_time             = Clock::now();
        last_tick_time         = start_time;
        next_tick_time         = start_time;
        time_ms                = 0.0;
        delta_time_ms          = 0.0;
        delta_time_smoothed_ms = 0.0;
        pacing_time_ms         = 0.0;
        reset_deadline         = true;
        has_sample             = false;
    }

    void Timer::PostTick()
    {
        const Clock::time_point pacing_start = Clock::now();
        Clock::time_point frame_end          = pacing_start;
        pacing_time_ms                      = 0.0;

        // the maximum value is the existing UI/settings sentinel for unlocked
        if (fps_limit < fps_max)
        {
            const Clock::duration interval = chrono::round<Clock::duration>(chrono::duration<double>(1.0 / fps_limit));
            if (reset_deadline)
            {
                next_tick_time = last_tick_time + interval;
                reset_deadline = false;
            }

            const bool missed_deadline = frame_end >= next_tick_time;
            if (!missed_deadline)
            {
                // SDL sleeps for the coarse portion and spins near the deadline.
                // Recheck our own clock so rounding cannot release a frame early.
                do
                {
                    const auto remaining = chrono::duration_cast<chrono::nanoseconds>(next_tick_time - frame_end).count();
                    if (remaining > 0)
                    {
                        SDL_DelayPrecise(static_cast<Uint64>(remaining));
                    }
                    frame_end = Clock::now();
                } while (frame_end < next_tick_time);

                pacing_time_ms = chrono::duration<double, milli>(frame_end - pacing_start).count();
            }

            // Preserve the cadence across small wake-up errors, but rebase after
            // slow frames or long scheduler stalls instead of issuing catch-up frames.
            next_tick_time = missed_deadline || frame_end - next_tick_time >= interval
                ? frame_end + interval
                : next_tick_time + interval;
        }

        // A single frame boundary avoids losing time between separate clock reads.
        // Keep raw deltas truthful; simulation-specific clamping belongs to consumers.
        delta_time_ms         = chrono::duration<double, milli>(frame_end - last_tick_time).count();
        time_ms               = chrono::duration<double, milli>(frame_end - start_time).count();
        delta_time_smoothed_ms = has_sample
            ? delta_time_smoothed_ms + (delta_time_ms - delta_time_smoothed_ms) * weight_delta
            : delta_time_ms;
        has_sample     = true;
        last_tick_time = frame_end;
    }

    void Timer::SetFpsLimit(float fps_in)
    {
        if (!isfinite(fps_in))
        {
            return;
        }

        if (fps_in < 0.0f) // negative -> match monitor's refresh rate
        {
            fps_in = get_monitor_fps();
        }

        // clamp to a minimum of 30 FPS to avoid unresponsiveness
        fps_in = clamp(fps_in, fps_min, fps_max);

        if (fps_limit == fps_in)
        {
            return;
        }

        fps_limit      = fps_in;
        reset_deadline = true;
        SP_LOG_INFO("Set to %.2f FPS", fps_limit);
    }

    float Timer::GetFpsLimit()
    {
        return fps_limit;
    }

    FpsLimitType Timer::GetFpsLimitType()
    {
        if (fps_limit == static_cast<float>(Display::GetRefreshRate()))
        {
            return FpsLimitType::FixedToMonitor;
        }

        if (fps_limit == fps_max)
        {
            return FpsLimitType::Unlocked;
        }

        return FpsLimitType::Fixed;
    }

    double Timer::GetPacingTimeMs()
    {
        return pacing_time_ms;
    }

    void Timer::OnVsyncToggled(const bool enabled)
    {
        if (vsync_enabled == enabled)
        {
            return;
        }
        vsync_enabled = enabled;

        if (enabled)
        {
            fps_limit_previous = fps_limit;
            SetFpsLimit(-1.0f);
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
