/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include <vector>
#include <string>

namespace car
{
    struct dyno_sample
    {
        float time, target_rpm, rpm, wheel_rpm, throttle;
        float combustion_nm, combustion_kw, axle_nm, axle_kw, motor_nm, motor_kw;
        float boost_bar, battery_soc, clutch_slip_rpm;
    };

    // Speed-controlled hub fixture. No tire contact, roller inertia or road load.
    // The production crank/clutch/shaft/motor integrator supplies every sample.
    struct dyno_state
    {
        bool mounted = false;
        bool running = false;
        bool sweep = true;
        int gear = 4; // internal index: 0 reverse, 1 neutral, 2 first
        float start_rpm = 1500, end_rpm = 8000, sweep_seconds = 15, throttle = 1;
        float time = 0, sample_clock = 0;
        std::string car_name, status = "Ready", export_path;
        int run_gear = 4;
        float run_ratio = 1, run_final_drive = 1;
        std::string previous_car;
        std::vector<dyno_sample> previous_samples;
        std::vector<dyno_sample> samples;
    };
}

namespace spartan
{
    class Car;
    class Physics;
    namespace car_hud { void draw_dyno_window(Car* car, Physics* physics); }
}
