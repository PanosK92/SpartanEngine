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

#include <cstdint>
#include <memory>

// procedural engine sound, every cylinder fires a blowdown pulse into its own exhaust runner,
// the runners meet in a per bank collector, pass a muffler and a tailpipe, on top sit the
// induction roar, the turbo, the valvetrain and the overrun pops, all driven by the car spec

namespace engine_sound
{
    namespace tuning
    {
        constexpr int max_cylinders = 16;
        constexpr int sample_rate = 48000;
    }

    // where the listener sits, decides how much body and cabin sit between them and the exhaust
    enum class listener_view
    {
        chase,
        hood,
        cabin
    };

    struct engine_config
    {
        int cylinder_count = 4;
        int bank_count = 1;
        float bank_angle_deg = 0.0f;
        float idle_rpm = 800.0f;
        float redline_rpm = 7000.0f;
        float max_rpm = 7500.0f;
        float displacement_l = 2.0f;
        float bore_mm = 86.0f;
        float stroke_mm = 86.0f;
        float compression_ratio = 10.0f;
        float primary_length_m = 0.45f;
        float collector_length_m = 2.0f;
        float tailpipe_length_m = 0.6f;
        // one is a quiet stock muffler, zero is a straight pipe
        float muffler_level = 1.0f;
        // Zero runner length derives an estimate from stroke. Angles are crank degrees.
        float intake_runner_length_m = 0.0f;
        float intake_valve_duration_deg = 220.0f;
        float combustion_variation = 0.025f;
        float crank_inertia = 0.2f;
        // Intervals in firing-order sequence, totaling 720; all zero means even firing.
        // Bank angle alone cannot tell us whether a crank uses split pins.
        float firing_intervals_deg[tuning::max_cylinders] = {};
        int firing_order[tuning::max_cylinders] = {};
        int cylinder_bank[tuning::max_cylinders] = {};
        bool turbo_enabled = false;
        bool turbo_bypass_valve = true; // a working bypass vents charge instead of compressor surge
        float boost_max_pressure = 0.0f;
        float boost_wastegate_rpm = 0.0f;
        // upgrade stages as a fraction of their maximum stage
        float engine_stage = 0.0f;
        float exhaust_stage = 0.0f;
        float intake_stage = 0.0f;
        float turbo_stage = 0.0f;

        engine_config();
        bool operator==(const engine_config& other) const;
        bool operator!=(const engine_config& other) const;
    };

    // live mix tuning, exposed in the hud
    struct runtime_params
    {
        float exhaust_level = 1.0f;
        float intake_level = 1.0f;
        float turbo_level = 1.0f;
        float mechanical_level = 1.0f;
        float pop_rate = 1.0f;
        float rasp = 1.0f;
        float cabin_mix = 1.0f;
        float master_gain = 1.0f;
    };

    struct debug_data
    {
        float rpm = 0.0f;
        float throttle = 0.0f;
        float load = 0.0f;
        float boost = 0.0f;
        float firing_freq = 0.0f;
        float exhaust_level = 0.0f;
        float intake_level = 0.0f;
        float turbo_level = 0.0f;
        float mechanical_level = 0.0f;
        float pop_level = 0.0f;
        float output_level = 0.0f;
        float output_peak = 0.0f;
        float limiter_gain = 1.0f;
        int pops_fired = 0;
        bool odd_fire = false;

        static constexpr int waveform_size = 512;
        float waveform[waveform_size] = {};
        int waveform_write_pos = 0;

        int dump_total = 0;
        int dump_progress = 0;
        bool dump_ready = false;
        std::uint64_t generate_calls = 0;
        std::uint64_t samples_generated = 0;
        bool initialized = false;
    };

    class synthesizer
    {
    public:
        synthesizer();
        ~synthesizer();

        synthesizer(const synthesizer&) = delete;
        synthesizer& operator=(const synthesizer&) = delete;

        runtime_params params;

        void initialize(int sample_rate = tuning::sample_rate);
        void configure(const engine_config& config);
        void set_parameters(
            float rpm,
            float throttle,
            float load,
            float boost_pressure,
            bool fuel_cut,
            int gear,
            bool shifting,
            listener_view view
        );
        void generate(
            float* output_buffer,
            int num_samples,
            bool stereo = true
        );
        void reset();

        bool is_initialized() const;
        const engine_config& get_config() const;
        const debug_data& get_debug() const;
        bool begin_dump(float seconds);
        bool dump_ready() const;
        bool save_dump(const char* path);

    private:
        class implementation;
        std::unique_ptr<implementation> m_implementation;
    };

    synthesizer& get_synthesizer();
    void initialize(int sample_rate = tuning::sample_rate);
    void configure(const engine_config& config);
    void set_parameters(
        float rpm,
        float throttle,
        float load,
        float boost,
        bool fuel_cut,
        int gear,
        bool shifting,
        listener_view view
    );
    void generate(
        float* buffer,
        int num_samples,
        bool stereo = true
    );
    void reset();
    const debug_data& get_debug();
}
