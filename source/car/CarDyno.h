/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
        float engine_net_nm, engine_net_kw; // after engine losses, before rotor acceleration
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
