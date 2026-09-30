/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// offline renderer for the procedural engine sound, drives the synthesizer with scripted controls
// derived from a .car file and writes the audio plus the control track the synthesizer saw

#include "../../source/car/CarEngineSoundSynthesis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr int sample_rate = 48000;
    // one control update per 10 ms, roughly what the game feeds per frame
    constexpr int block_size = 480;

    float clamp01(float v)
    {
        return std::clamp(v, 0.0f, 1.0f);
    }

    float smoothstep(float edge0, float edge1, float x)
    {
        float t = clamp01((x - edge0) / (edge1 - edge0));
        return t * t * (3.0f - 2.0f * t);
    }

    struct car_file
    {
        std::map<std::string, std::string> attributes;

        bool load(const std::string& path)
        {
            std::ifstream file(path);
            if (!file)
            {
                return false;
            }
            std::stringstream text;
            text << file.rdbuf();
            const std::string content = text.str();
            // later attributes override earlier ones, same as the engine's loader
            const std::regex pattern("([A-Za-z_][A-Za-z0-9_]*)=\"([^\"]*)\"");
            for (auto it = std::sregex_iterator(content.begin(), content.end(), pattern); it != std::sregex_iterator(); ++it)
            {
                // the car's name is the first one, on the root node
                if ((*it)[1].str() == "name" && attributes.count("name"))
                {
                    continue;
                }
                attributes[(*it)[1].str()] = (*it)[2].str();
            }
            return true;
        }

        float get(const char* name, float fallback) const
        {
            auto it = attributes.find(name);
            return it == attributes.end() ? fallback : std::stof(it->second);
        }

        bool get_bool(const char* name, bool fallback) const
        {
            auto it = attributes.find(name);
            return it == attributes.end() ? fallback : (it->second == "true" || it->second == "1");
        }

        int get_array(const char* name, float* out, int capacity) const
        {
            auto it = attributes.find(name);
            if (it == attributes.end())
            {
                return 0;
            }
            std::stringstream stream(it->second);
            std::string token;
            int count = 0;
            while (count < capacity && std::getline(stream, token, ','))
            {
                out[count++] = std::stof(token);
            }
            return count;
        }
    };

    struct car_spec
    {
        engine_sound::engine_config config;
        float peak_torque = 300.0f;
        float peak_torque_rpm = 4000.0f;
        float boost_min_rpm = 2500.0f;
        std::vector<float> map_rpm;
        std::vector<float> map_torque;

        // full load torque as a fraction of peak, what the game feeds as load at wide open throttle
        float torque_norm(float rpm) const
        {
            if (map_rpm.size() >= 2)
            {
                if (rpm <= map_rpm.front())
                {
                    return map_torque.front() / peak_torque;
                }
                for (size_t i = 1; i < map_rpm.size(); i++)
                {
                    if (rpm <= map_rpm[i])
                    {
                        float t = (rpm - map_rpm[i - 1]) / std::max(map_rpm[i] - map_rpm[i - 1], 1.0f);
                        return (map_torque[i - 1] + (map_torque[i] - map_torque[i - 1]) * t) / peak_torque;
                    }
                }
                return map_torque.back() / peak_torque;
            }
            const engine_sound::engine_config& c = config;
            if (rpm < peak_torque_rpm)
            {
                return 0.6f + 0.4f * smoothstep(c.idle_rpm, peak_torque_rpm, rpm);
            }
            return 1.0f - 0.15f * clamp01((rpm - peak_torque_rpm) / std::max(c.redline_rpm - peak_torque_rpm, 1.0f));
        }
    };

    struct stage_overrides
    {
        float engine = 0.0f;
        float exhaust = 0.0f;
        float intake = 0.0f;
        float turbo = 0.0f;
        float muffler = -1.0f;
    };

    car_spec make_spec(const car_file& car, const stage_overrides& stages)
    {
        car_spec spec;
        engine_sound::engine_config& c = spec.config;
        c.cylinder_count = static_cast<int>(car.get("engine_sound_cylinders", 4.0f));
        c.bank_count = static_cast<int>(car.get("engine_sound_banks", 1.0f));
        c.bank_angle_deg = car.get("engine_bank_angle_deg", 0.0f);
        c.idle_rpm = car.get("engine_idle_rpm", 800.0f);
        c.redline_rpm = car.get("engine_redline_rpm", 7000.0f);
        c.max_rpm = car.get("engine_max_rpm", c.redline_rpm + 300.0f);
        c.displacement_l = car.get("engine_displacement_l", 2.0f);
        c.bore_mm = car.get("engine_bore_mm", 86.0f);
        c.stroke_mm = car.get("engine_stroke_mm", 86.0f);
        c.compression_ratio = car.get("engine_compression_ratio", 10.0f);
        c.primary_length_m = car.get("exhaust_primary_length_m", 0.45f);
        c.collector_length_m = car.get("exhaust_collector_length_m", 2.0f);
        c.tailpipe_length_m = car.get("exhaust_tailpipe_length_m", 0.6f);
        c.muffler_level = car.get("exhaust_muffler_level", 1.0f);
        c.intake_runner_length_m = car.get("intake_runner_length_m", 0.0f);
        c.intake_valve_duration_deg = car.get("intake_valve_duration_deg", 220.0f);
        c.combustion_variation = car.get("engine_combustion_variation", 0.025f);
        c.crank_inertia = car.get("engine_inertia", 0.2f);
        c.turbo_bypass_valve = car.get_bool("turbo_bypass_valve", true);
        c.boost_max_pressure = car.get("boost_max_pressure", 0.0f);
        c.turbo_enabled = car.get_bool("turbo_enabled", false) && c.boost_max_pressure > 0.0f;
        c.boost_wastegate_rpm = car.get("boost_wastegate_rpm", 0.0f);

        float values[engine_sound::tuning::max_cylinders] = {};
        int count = car.get_array("engine_firing_order", values, engine_sound::tuning::max_cylinders);
        for (int i = 0; i < count; i++)
        {
            c.firing_order[i] = static_cast<int>(values[i]);
        }
        count = car.get_array("engine_cylinder_bank", values, engine_sound::tuning::max_cylinders);
        for (int i = 0; i < count; i++)
        {
            c.cylinder_bank[i] = static_cast<int>(values[i]);
        }
        std::fill(std::begin(values), std::end(values), 0.0f);
        count = car.get_array("engine_firing_intervals_deg", values, engine_sound::tuning::max_cylinders);
        for (int i = 0; i < count; i++)
        {
            c.firing_intervals_deg[i] = values[i];
        }

        // same muffler opening as the exhaust upgrade in the simulation
        c.engine_stage = stages.engine;
        c.exhaust_stage = stages.exhaust;
        c.intake_stage = stages.intake;
        c.turbo_stage = stages.turbo;
        c.muffler_level *= 1.0f - 0.9f * stages.exhaust;
        if (stages.muffler >= 0.0f)
        {
            c.muffler_level = stages.muffler;
        }

        spec.peak_torque = car.get("engine_peak_torque", 300.0f);
        spec.peak_torque_rpm = car.get("engine_peak_torque_rpm", 4000.0f);
        spec.boost_min_rpm = car.get("boost_min_rpm", 2500.0f);
        float map_rpm[16] = {};
        float map_torque[16] = {};
        int map_count = std::min(car.get_array("engine_map_rpm", map_rpm, 16), car.get_array("engine_map_torque", map_torque, 16));
        spec.map_rpm.assign(map_rpm, map_rpm + map_count);
        spec.map_torque.assign(map_torque, map_torque + map_count);
        return spec;
    }

    struct controls
    {
        float rpm = 0.0f;
        float throttle = 0.0f;
        float load = 0.0f;
        float boost = 0.0f;
        float gearbox_rpm = 0.0f;
        int gear = 1;
        bool fuel_cut = false;
        bool shifting = false;
        bool overrun = false;
    };

    // a crude engine and driveline, only there to make the controls move the way the game moves them
    struct scenario_state
    {
        float time = 0.0f;
        float rpm = 0.0f;
        float boost = 0.0f;
        float throttle = 0.0f;
        bool cut = false;
        int gear = 1;
        float shift_timer = -1.0f;
    };

    struct scenario
    {
        const char* name;
        float seconds;
        bool starts_engine;
        std::function<void(const car_spec&, scenario_state&, float, controls&)> step;
    };

    // the game's own load mapping, see Car.cpp: max(throttle * 0.15, output torque / peak torque)
    float game_load(float throttle, float torque_norm)
    {
        return clamp01(std::max(throttle * 0.15f, torque_norm));
    }

    void finish_controls(const car_spec& spec, scenario_state& s, float dt, float torque_norm, controls& out)
    {
        const engine_sound::engine_config& c = spec.config;
        float boost_target = 0.0f;
        if (c.turbo_enabled)
        {
            boost_target = c.boost_max_pressure * smoothstep(spec.boost_min_rpm * 0.6f, spec.boost_min_rpm + 800.0f, s.rpm) * s.throttle * s.throttle;
        }
        // spool lag, a turbo takes a few tenths of a second to follow
        float rate = boost_target > s.boost ? 3.0f : 8.0f;
        s.boost += (boost_target - s.boost) * (1.0f - expf(-rate * dt));

        out.rpm = s.rpm;
        out.throttle = s.throttle;
        out.load = game_load(s.throttle, torque_norm);
        out.boost = s.boost;
        out.fuel_cut = s.cut;
        out.gear = s.gear;
        out.overrun = s.throttle < 0.05f && s.rpm > c.idle_rpm + 300.0f && torque_norm <= 0.0f;
        out.gearbox_rpm = out.shifting || s.gear == 0 ? 0.0f : s.rpm;
    }

    std::vector<scenario> make_scenarios()
    {
        std::vector<scenario> list;

        list.push_back({ "idle", 6.0f, false, [](const car_spec& spec, scenario_state& s, float dt, controls& out)
        {
            s.rpm = spec.config.idle_rpm;
            s.throttle = 0.0f;
            s.gear = 0;
            finish_controls(spec, s, dt, 0.06f, out);
        } });

        list.push_back({ "start", 5.0f, true, [](const car_spec& spec, scenario_state& s, float dt, controls& out)
        {
            s.rpm = spec.config.idle_rpm;
            s.throttle = 0.0f;
            s.gear = 0;
            finish_controls(spec, s, dt, 0.06f, out);
        } });

        // a slow full load sweep, a dyno pull, the reference for order analysis
        list.push_back({ "wot_sweep", 16.0f, false, [](const car_spec& spec, scenario_state& s, float dt, controls& out)
        {
            const engine_sound::engine_config& c = spec.config;
            float t = clamp01((s.time - 1.0f) / 14.0f);
            s.rpm = c.idle_rpm + (c.redline_rpm - c.idle_rpm) * t;
            s.throttle = s.time < 0.8f ? 0.0f : std::min(1.0f, (s.time - 0.8f) / 0.2f);
            s.gear = 3;
            finish_controls(spec, s, dt, s.throttle > 0.0f ? spec.torque_norm(s.rpm) * s.throttle : 0.06f, out);
        } });

        list.push_back({ "part_sweep", 16.0f, false, [](const car_spec& spec, scenario_state& s, float dt, controls& out)
        {
            const engine_sound::engine_config& c = spec.config;
            float t = clamp01((s.time - 1.0f) / 14.0f);
            s.rpm = c.idle_rpm + (c.redline_rpm * 0.75f - c.idle_rpm) * t;
            s.throttle = s.time < 0.8f ? 0.0f : 0.3f;
            s.gear = 3;
            finish_controls(spec, s, dt, s.throttle > 0.0f ? spec.torque_norm(s.rpm) * 0.3f : 0.06f, out);
        } });

        // hold near the top, lift, and let the car drag the engine down in gear
        list.push_back({ "overrun", 8.0f, false, [](const car_spec& spec, scenario_state& s, float dt, controls& out)
        {
            const engine_sound::engine_config& c = spec.config;
            if (s.time < 1.5f)
            {
                s.rpm = c.redline_rpm * 0.9f;
                s.throttle = 1.0f;
            }
            else
            {
                s.throttle = std::max(0.0f, s.throttle - dt / 0.08f);
                float target = c.idle_rpm;
                s.rpm = std::max(target, s.rpm - (900.0f + 0.22f * s.rpm) * dt);
            }
            s.gear = s.rpm > c.idle_rpm + 250.0f ? 3 : 0;
            float torque = s.throttle > 0.05f ? spec.torque_norm(s.rpm) * s.throttle : (s.gear == 0 ? 0.06f : -0.2f);
            finish_controls(spec, s, dt, torque, out);
        } });

        // bouncing off the rev limiter in gear
        list.push_back({ "limiter", 4.0f, false, [](const car_spec& spec, scenario_state& s, float dt, controls& out)
        {
            const engine_sound::engine_config& c = spec.config;
            if (s.time == 0.0f)
            {
                s.rpm = c.redline_rpm * 0.85f;
            }
            s.throttle = 1.0f;
            s.gear = 3;
            if (s.rpm >= c.redline_rpm)
            {
                s.cut = true;
            }
            else if (s.rpm < c.redline_rpm - 150.0f)
            {
                s.cut = false;
            }
            s.rpm += (s.cut ? -2500.0f : 2200.0f) * dt;
            finish_controls(spec, s, dt, s.cut ? -0.1f : spec.torque_norm(s.rpm), out);
        } });

        // free revving in neutral, short stabs of throttle
        list.push_back({ "blips", 8.0f, false, [](const car_spec& spec, scenario_state& s, float dt, controls& out)
        {
            const engine_sound::engine_config& c = spec.config;
            if (s.time == 0.0f)
            {
                s.rpm = c.idle_rpm;
            }
            float phase = fmodf(s.time, 2.0f);
            float stab = s.time < 1.0f ? 0.0f : (phase > 0.5f && phase < 0.5f + 0.12f + 0.06f * fmodf(s.time / 2.0f, 3.0f) ? 1.0f : 0.0f);
            s.throttle += (stab - s.throttle) * (1.0f - expf(-dt / 0.03f));
            s.gear = 0;
            if (s.throttle > 0.1f)
            {
                s.rpm = std::min(c.redline_rpm, s.rpm + 14000.0f * s.throttle * spec.torque_norm(s.rpm) / std::max(c.crank_inertia * 4.0f, 0.5f) * dt);
            }
            else
            {
                s.rpm = std::max(c.idle_rpm, s.rpm - (1800.0f + 0.5f * s.rpm) * dt);
            }
            float torque = s.throttle > 0.1f ? spec.torque_norm(s.rpm) * s.throttle : (s.rpm > c.idle_rpm + 50.0f ? -0.1f : 0.06f);
            finish_controls(spec, s, dt, torque, out);
        } });

        // a launch through four gears, lifting for each shift
        list.push_back({ "pulls", 14.0f, false, [](const car_spec& spec, scenario_state& s, float dt, controls& out)
        {
            const engine_sound::engine_config& c = spec.config;
            const float rates[4] = { 3600.0f, 2300.0f, 1600.0f, 1100.0f };
            if (s.time == 0.0f)
            {
                s.rpm = c.idle_rpm * 2.5f;
                s.gear = 1;
            }
            out.shifting = false;
            if (s.shift_timer >= 0.0f)
            {
                s.shift_timer += dt;
                out.shifting = true;
                s.throttle = std::max(0.0f, s.throttle - dt / 0.05f);
                // clutch open, the engine drops to the next gear's speed when it closes
                if (s.shift_timer > 0.22f)
                {
                    s.shift_timer = -1.0f;
                    s.rpm *= 0.68f;
                    s.gear = std::min(s.gear + 1, 4);
                }
            }
            else if (s.time > 0.6f)
            {
                s.throttle = std::min(1.0f, s.throttle + dt / 0.08f);
                s.rpm += rates[std::clamp(s.gear - 1, 0, 3)] * spec.torque_norm(s.rpm) * dt;
                if (s.rpm >= c.redline_rpm * 0.97f && s.gear < 4)
                {
                    s.shift_timer = 0.0f;
                }
                s.rpm = std::min(s.rpm, c.redline_rpm);
            }
            float torque = s.throttle > 0.05f ? spec.torque_norm(s.rpm) * s.throttle : -0.1f;
            bool shifting = out.shifting;
            finish_controls(spec, s, dt, torque, out);
            out.shifting = shifting;
            if (shifting)
            {
                out.gearbox_rpm = 0.0f;
            }
        } });

        return list;
    }

    bool write_wav_float(const std::string& path, const std::vector<float>& interleaved)
    {
        FILE* file = nullptr;
        fopen_s(&file, path.c_str(), "wb");
        if (!file)
        {
            return false;
        }
        const std::uint16_t channels = 2;
        const std::uint16_t bits = 32;
        const std::uint16_t format = 3; // ieee float
        const std::uint16_t block_align = channels * 4;
        const std::uint32_t data_bytes = static_cast<std::uint32_t>(interleaved.size() * 4);
        const std::uint32_t riff_size = 36 + data_bytes;
        const std::uint32_t rate = sample_rate;
        const std::uint32_t byte_rate = rate * block_align;
        const std::uint32_t format_size = 16;
        fwrite("RIFF", 1, 4, file);
        fwrite(&riff_size, 4, 1, file);
        fwrite("WAVEfmt ", 1, 8, file);
        fwrite(&format_size, 4, 1, file);
        fwrite(&format, 2, 1, file);
        fwrite(&channels, 2, 1, file);
        fwrite(&rate, 4, 1, file);
        fwrite(&byte_rate, 4, 1, file);
        fwrite(&block_align, 2, 1, file);
        fwrite(&bits, 2, 1, file);
        fwrite("data", 1, 4, file);
        fwrite(&data_bytes, 4, 1, file);
        fwrite(interleaved.data(), 4, interleaved.size(), file);
        fclose(file);
        return true;
    }

    std::string argument(int argc, char** argv, const char* name, const char* fallback)
    {
        const std::string prefix = std::string("--") + name + "=";
        for (int i = 1; i < argc; i++)
        {
            if (strncmp(argv[i], prefix.c_str(), prefix.size()) == 0)
            {
                return argv[i] + prefix.size();
            }
        }
        return fallback;
    }

    engine_sound::listener_view parse_view(const std::string& view)
    {
        if (view == "hood")
        {
            return engine_sound::listener_view::hood;
        }
        if (view == "cabin")
        {
            return engine_sound::listener_view::cabin;
        }
        return engine_sound::listener_view::chase;
    }
}

int main(int argc, char** argv)
{
    const std::string car_path = argument(argc, argv, "car", "");
    const std::string scenario_name = argument(argc, argv, "scenario", "wot_sweep");
    const std::string out_base = argument(argc, argv, "out", "");
    const engine_sound::listener_view view = parse_view(argument(argc, argv, "view", "chase"));
    stage_overrides stages;
    stages.engine = std::stof(argument(argc, argv, "engine_stage", "0"));
    stages.exhaust = std::stof(argument(argc, argv, "exhaust_stage", "0"));
    stages.intake = std::stof(argument(argc, argv, "intake_stage", "0"));
    stages.turbo = std::stof(argument(argc, argv, "turbo_stage", "0"));
    stages.muffler = std::stof(argument(argc, argv, "muffler", "-1"));

    std::vector<scenario> scenarios = make_scenarios();
    if (car_path.empty() || out_base.empty())
    {
        printf("usage: engine_sound_render --car=<file.car> --out=<path without extension> [--scenario=name] [--view=chase|hood|cabin]\n");
        printf("       [--engine_stage=0..1] [--exhaust_stage=0..1] [--intake_stage=0..1] [--turbo_stage=0..1] [--muffler=0..1]\n");
        printf("scenarios:");
        for (const scenario& s : scenarios)
        {
            printf(" %s", s.name);
        }
        printf("\n");
        return 1;
    }

    car_file car;
    if (!car.load(car_path))
    {
        printf("failed to read %s\n", car_path.c_str());
        return 1;
    }
    const car_spec spec = make_spec(car, stages);

    const scenario* chosen = nullptr;
    for (const scenario& s : scenarios)
    {
        if (scenario_name == s.name)
        {
            chosen = &s;
        }
    }
    if (!chosen)
    {
        printf("unknown scenario %s\n", scenario_name.c_str());
        return 1;
    }

    engine_sound::synthesizer synth;
    synth.initialize(sample_rate);
    synth.configure(spec.config);

    scenario_state state;
    controls first;
    chosen->step(spec, state, 0.0f, first);
    synth.set_parameters(first.rpm, first.throttle, first.load, first.boost, first.fuel_cut, first.gear, first.shifting, view, first.gearbox_rpm, first.overrun, 1.0f);
    if (chosen->starts_engine)
    {
        synth.start();
    }
    else
    {
        synth.prime();
    }

    // exercises the synthesizer's own capture path, the one the engine_sound_capture mcp tool uses
    const bool dump = argument(argc, argv, "dump", "0") == "1";
    if (dump && !synth.begin_dump(chosen->seconds - 0.05f))
    {
        printf("begin_dump failed\n");
        return 1;
    }

    const int blocks = static_cast<int>(chosen->seconds * sample_rate / block_size);
    const float dt = static_cast<float>(block_size) / sample_rate;
    std::vector<float> audio(static_cast<size_t>(blocks) * block_size * 2);
    std::string csv = "time,rpm,rpm_heard,throttle,load,boost,fuel_cut,overrun,shifting,gear,output_rms,output_peak,limiter_gain,pops_fired,exhaust_rms,intake_rms,turbo_rms,mechanical_rms\n";

    for (int b = 0; b < blocks; b++)
    {
        controls c;
        if (b > 0)
        {
            chosen->step(spec, state, dt, c);
        }
        else
        {
            c = first;
        }
        synth.set_parameters(c.rpm, c.throttle, c.load, c.boost, c.fuel_cut, c.gear, c.shifting, view, c.gearbox_rpm, c.overrun, 1.0f);
        synth.generate(audio.data() + static_cast<size_t>(b) * block_size * 2, block_size, true);
        state.time += dt;

        const engine_sound::debug_data debug = synth.get_debug();
        char row[256];
        snprintf(row, sizeof(row), "%.4f,%.1f,%.1f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%.5f,%.5f,%.4f,%d,%.5f,%.5f,%.5f,%.5f\n",
            b * dt, c.rpm, debug.rpm, c.throttle, c.load, c.boost, c.fuel_cut ? 1 : 0, c.overrun ? 1 : 0, c.shifting ? 1 : 0, c.gear,
            debug.output_level, debug.output_peak, debug.limiter_gain, debug.pops_fired,
            debug.exhaust_level, debug.intake_level, debug.turbo_level, debug.mechanical_level);
        csv += row;
    }

    if (!write_wav_float(out_base + ".wav", audio))
    {
        printf("failed to write %s.wav\n", out_base.c_str());
        return 1;
    }
    std::ofstream(out_base + ".csv") << csv;
    if (dump && (!synth.dump_ready() || !synth.save_dump((out_base + "_dump.wav").c_str())))
    {
        printf("dump was not saved\n");
        return 1;
    }

    const engine_sound::engine_config& c = spec.config;
    std::ostringstream meta;
    meta << "{\"car\":\"" << car.attributes["name"] << "\",\"scenario\":\"" << chosen->name << "\",\"sample_rate\":" << sample_rate
         << ",\"cylinders\":" << c.cylinder_count << ",\"banks\":" << c.bank_count << ",\"idle_rpm\":" << c.idle_rpm
         << ",\"redline_rpm\":" << c.redline_rpm << ",\"muffler_level\":" << c.muffler_level << ",\"turbo\":" << (c.turbo_enabled ? "true" : "false") << "}\n";
    std::ofstream(out_base + ".json") << meta.str();
    if (dump)
    {
        std::ofstream(out_base + "_dump.json") << meta.str();
    }
    printf("wrote %s.wav (%s, %.1f s)\n", out_base.c_str(), chosen->name, chosen->seconds);
    return 0;
}
