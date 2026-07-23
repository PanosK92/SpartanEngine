/*
Copyright(c) 2015-2026 Panos Karabelas
*/
#pragma once

namespace spartan
{
    class Car;
    class Physics;

    namespace car_bench
    {
        enum class scenario : int
        {
            settle = 0,
            launch,
            hard_brake,
            lift_turn,
            coast_hold_gear,
            power_turn,
            count
        };

        struct scenario_result
        {
            bool ran = false;
            bool passed = false;
            float fail_time = 0.0f;
            char fail_code[48] = {};
            char fail_metric[64] = {};
            char detail[160] = {};
            char hint[192] = {};
            char likely_files[192] = {};
        };

        struct ui_state
        {
            bool window_open = false;
            bool running = false;
            bool stop_requested = false;
            bool write_telemetry = false;
            bool enabled[static_cast<int>(scenario::count)];
            scenario_result results[static_cast<int>(scenario::count)];
            int current_scenario = -1;
            float scenario_time = 0.0f;
            float sim_time_total = 0.0f;
            float wall_time_total = 0.0f;
            int pass_count = 0;
            int fail_count = 0;
            char car_name[96] = {};
            char status[128] = {};
            char error[256] = {};
            char digest_path[128] = {};
            char feedback_path[128] = {};
            char agent_prompt[512] = {};
        };

        ui_state& get_state();

        const char* scenario_name(scenario id);
        void open_window();
        void start(Car* car, Physics* physics);
        void stop(Car* car, Physics* physics);
        // advance scripted steps while running, call once per frame from the hud
        void tick(Car* car, Physics* physics);
        void export_report();
        // writes json + markdown the agent can read for the next fix loop
        void write_agent_digest();
        void draw_window(Car* car, Physics* physics);
    }
}
