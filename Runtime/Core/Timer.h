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

//= INCLUDES ==========
#include "ISubsystem.h"
#include <chrono>
//=====================

namespace Spartan
{
    class Context;

    enum FPS_Policy
    {
        Fps_Unlocked,
        Fps_Fixed,
        Fps_FixedMonitor
    };

    class SPARTAN_CLASS Timer : public ISubsystem
    {
    public:
        Timer(Context* context);
        ~Timer() = default;

        //= ISybsystem ======================
        void Tick(float delta_time) override;
        //===================================

        //= FPS ============================================
        void SetTargetFps(double fps);
        auto GetTargetFps() const   { return m_fps_target; }
        auto GetMinFps() const      { return m_fps_min; }
        auto GetFpsPolicy() const   { return m_fps_policy; }
        //==================================================

        auto GetTimeMs()                const { return m_time_ms; }
        auto GetTimeSec()               const { return static_cast<float>(m_time_ms / 1000.0); }
        auto GetDeltaTimeMs()           const { return m_delta_time_ms; }
        auto GetDeltaTimeSec()          const { return static_cast<float>(m_delta_time_ms / 1000.0); }
        auto GetDeltaTimeSmoothedMs()   const { return m_delta_time_smoothed_ms; }
        auto GetDeltaTimeSmoothedSec()  const { return static_cast<float>(m_delta_time_smoothed_ms / 1000.0); }

    private:
        // Frame time
        std::chrono::high_resolution_clock::time_point m_time_start;
        std::chrono::high_resolution_clock::time_point m_time_frame_start;
        std::chrono::high_resolution_clock::time_point m_time_frame_end;
        double m_time_ms                = 0.0f;
        double m_delta_time_ms          = 0.0f;
        double m_delta_time_smoothed_ms = 0.0f;
        double m_sleep_overhead         = 0.0f;

        // FPS
        double m_fps_min                = 30.0;
        double m_fps_max                = 1000.0;
        double m_fps_target             = m_fps_max;
        bool m_user_selected_fps_target = false;
        FPS_Policy m_fps_policy         = Fps_Unlocked;
    };
}
