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

namespace engine_sound
{
    namespace tuning
    {
        constexpr int max_cylinders = 16;
        constexpr int sample_rate = 48000;
    }

    struct engine_config
    {
        int cylinder_count = 4;
        int bank_count = 1;
        float idle_rpm = 800.0f;
        float redline_rpm = 7000.0f;
        float max_rpm = 7500.0f;
        float displacement_l = 2.0f;
        float bore_mm = 86.0f;
        float stroke_mm = 86.0f;
        float compression_ratio = 10.0f;
        float primary_length_m = 0.45f;
        float collector_length_m = 2.0f;
        int firing_order[tuning::max_cylinders] = {};
        int cylinder_bank[tuning::max_cylinders] = {};

        engine_config();
    };

    struct runtime_params
    {
        float convolution_wet = 1.0f;
        float df_f_mix = 0.01f;
        float air_noise = 1.0f;
        float leveler_target = 30000.0f / 32767.0f;
    };

    struct debug_data
    {
        float rpm = 0.0f;
        float throttle = 0.0f;
        float load = 0.0f;
        float boost = 0.0f;
        float firing_freq = 0.0f;
        float exhaust_level = 0.0f;
        float output_level = 0.0f;
        float output_peak = 0.0f;
        float leveler_gain = 1.0f;

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

        void initialize(
            int sample_rate = tuning::sample_rate,
            const char* impulse_response_path = nullptr
        );
        void configure(const engine_config& config);
        void set_parameters(
            float rpm,
            float throttle,
            float load,
            float boost_pressure,
            float crank_angle,
            float torque_normalized,
            bool fuel_cut
        );
        void generate(
            float* output_buffer,
            int num_samples,
            bool stereo = true
        );
        void reset();

        bool is_initialized() const;
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
        float crank_angle,
        float torque_normalized,
        bool fuel_cut
    );
    void generate(
        float* buffer,
        int num_samples,
        bool stereo = true
    );
    void reset();
    const debug_data& get_debug();
}
