/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

namespace spartan
{
    enum class FpsLimitType
    {
        Unlocked,
        Fixed,
        FixedToMonitor
    };

    class Timer
    {
    public:
        static void Initialize();
        // Start a fresh timing epoch without changing the configured FPS limit.
        // Call immediately before entering the frame loop to exclude loading time.
        static void Reset();
        static void PostTick();

        // FPS Limit
        static void SetFpsLimit(float fps); // negative: monitor, [30, 10000): capped, >=10000: unlocked
        static float GetFpsLimit();
        static FpsLimitType GetFpsLimitType();
        static void OnVsyncToggled(const bool enabled);

        // Frame-boundary snapshots in a monotonic clock domain. Raw deltas include
        // pacing and stalls; smoothed deltas are for display, not simulation.
        static double GetTimeMs();
        static double GetTimeSec();
        static double GetDeltaTimeMs();
        static double GetDeltaTimeSec();
        static double GetDeltaTimeSmoothedMs();
        static double GetDeltaTimeSmoothedSec();
        static double GetPacingTimeMs();
    };
}
