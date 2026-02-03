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

#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>
#include "../../editor/ImGui/Source/imgui.h"

namespace engine_sound
{
    // synthesis parameters - visceral v12 with bite
    namespace tuning
    {
        constexpr int   cylinder_count       = 12;
        constexpr int   sample_rate          = 48000;
        constexpr float idle_rpm             = 1000.0f;
        constexpr float redline_rpm          = 9250.0f;
        constexpr float max_rpm              = 9500.0f;

        // frequency range - maps rpm to fundamental frequency
        constexpr float idle_base_freq       = 40.0f;   // idle rumble
        constexpr float redline_base_freq    = 220.0f;  // redline scream

        // harmonic gains - more upper harmonics for presence
        constexpr float harmonic_1_gain      = 1.0f;    // fundamental
        constexpr float harmonic_2_gain      = 0.8f;    // octave
        constexpr float harmonic_3_gain      = 0.55f;   // character
        constexpr float harmonic_4_gain      = 0.4f;    // bite
        constexpr float harmonic_5_gain      = 0.25f;   // presence
        constexpr float harmonic_6_gain      = 0.15f;   // air

        // waveform character - how much sawtooth vs sine (0 = pure sine, 1 = pure saw)
        constexpr float saw_amount_idle      = 0.15f;   // slight grit at idle
        constexpr float saw_amount_redline   = 0.4f;    // aggressive at high rpm

        // exhaust crackle and pop
        constexpr float exhaust_noise        = 0.08f;   // broadband exhaust texture
        constexpr float crackle_amount       = 0.12f;   // pops on decel

        // volume
        constexpr float idle_volume          = 0.5f;
        constexpr float redline_volume       = 1.0f;
        constexpr float throttle_response    = 10.0f;
    }

    // debug data for visualization
    struct debug_data
    {
        // current state
        float rpm             = 0.0f;
        float throttle        = 0.0f;
        float load            = 0.0f;
        float boost           = 0.0f;
        float firing_freq     = 0.0f;

        // component levels (rms over last chunk)
        float combustion_level = 0.0f;
        float exhaust_level    = 0.0f;
        float induction_level  = 0.0f;
        float mechanical_level = 0.0f;
        float turbo_level      = 0.0f;
        float output_level     = 0.0f;

        // peak values
        float output_peak      = 0.0f;

        // waveform buffer for visualization
        static constexpr int waveform_size = 512;
        float waveform[waveform_size]      = {};
        int   waveform_write_pos           = 0;

        // call counter
        uint64_t generate_calls = 0;
        uint64_t samples_generated = 0;

        bool initialized = false;
    };

    // biquad filter using direct form 1 for resonance shaping
    struct biquad_filter
    {
        float b0 = 1, b1 = 0, b2 = 0;
        float a1 = 0, a2 = 0;
        // input history
        float x1 = 0, x2 = 0;
        // output history
        float y1 = 0, y2 = 0;

        void set_bandpass(float freq, float q, float sample_rate)
        {
            // clamp frequency to valid range
            freq = std::clamp(freq, 20.0f, sample_rate * 0.45f);
            q = std::max(q, 0.1f);

            float omega = 2.0f * 3.14159265f * freq / sample_rate;
            float sin_omega = sinf(omega);
            float cos_omega = cosf(omega);
            float alpha = sin_omega / (2.0f * q);

            float a0 = 1.0f + alpha;
            b0 = (alpha) / a0;
            b1 = 0.0f;
            b2 = (-alpha) / a0;
            a1 = (-2.0f * cos_omega) / a0;
            a2 = (1.0f - alpha) / a0;
        }

        void set_lowpass(float freq, float q, float sample_rate)
        {
            freq = std::clamp(freq, 20.0f, sample_rate * 0.45f);
            q = std::max(q, 0.1f);

            float omega = 2.0f * 3.14159265f * freq / sample_rate;
            float sin_omega = sinf(omega);
            float cos_omega = cosf(omega);
            float alpha = sin_omega / (2.0f * q);

            float a0 = 1.0f + alpha;
            b0 = ((1.0f - cos_omega) / 2.0f) / a0;
            b1 = (1.0f - cos_omega) / a0;
            b2 = ((1.0f - cos_omega) / 2.0f) / a0;
            a1 = (-2.0f * cos_omega) / a0;
            a2 = (1.0f - alpha) / a0;
        }

        void set_highpass(float freq, float q, float sample_rate)
        {
            freq = std::clamp(freq, 20.0f, sample_rate * 0.45f);
            q = std::max(q, 0.1f);

            float omega = 2.0f * 3.14159265f * freq / sample_rate;
            float sin_omega = sinf(omega);
            float cos_omega = cosf(omega);
            float alpha = sin_omega / (2.0f * q);

            float a0 = 1.0f + alpha;
            b0 = ((1.0f + cos_omega) / 2.0f) / a0;
            b1 = -(1.0f + cos_omega) / a0;
            b2 = ((1.0f + cos_omega) / 2.0f) / a0;
            a1 = (-2.0f * cos_omega) / a0;
            a2 = (1.0f - alpha) / a0;
        }

        float process(float input)
        {
            // direct form 1: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
            float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

            // update history
            x2 = x1;
            x1 = input;
            y2 = y1;
            y1 = output;

            // prevent denormals and clamp to prevent runaway
            if (fabsf(y1) < 1e-15f) y1 = 0.0f;
            if (fabsf(y2) < 1e-15f) y2 = 0.0f;
            if (fabsf(y1) > 10.0f) y1 = (y1 > 0) ? 10.0f : -10.0f;
            if (fabsf(y2) > 10.0f) y2 = (y2 > 0) ? 10.0f : -10.0f;

            return output;
        }

        void reset()
        {
            x1 = x2 = y1 = y2 = 0.0f;
        }
    };

    // oscillator with phase accumulator
    struct oscillator
    {
        float phase      = 0.0f;
        float phase_inc  = 0.0f;
        float frequency  = 0.0f;

        void set_frequency(float freq, float sample_rate)
        {
            frequency = freq;
            phase_inc = freq / sample_rate;
        }

        // sine wave
        float sine()
        {
            float value = sinf(phase * 2.0f * 3.14159265f);
            advance();
            return value;
        }

        // pulse wave with variable duty cycle
        float pulse(float duty = 0.5f)
        {
            float value = (phase < duty) ? 1.0f : -1.0f;
            advance();
            return value;
        }

        // sawtooth wave
        float saw()
        {
            float value = 2.0f * phase - 1.0f;
            advance();
            return value;
        }

        // combustion pulse - shaped impulse for engine firing
        float combustion_pulse(float attack, float decay)
        {
            float value;
            if (phase < attack)
            {
                // attack phase - quick rise
                value = phase / attack;
            }
            else if (phase < attack + decay)
            {
                // decay phase - exponential falloff
                float t = (phase - attack) / decay;
                value = expf(-3.0f * t);
            }
            else
            {
                value = 0.0f;
            }
            advance();
            return value;
        }

        void advance()
        {
            phase += phase_inc;
            while (phase >= 1.0f) phase -= 1.0f;
        }

        void reset()
        {
            phase = 0.0f;
        }
    };

    // simple pseudo-random noise generator
    struct noise_generator
    {
        uint32_t seed = 12345;

        float white()
        {
            // xorshift32
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return (static_cast<float>(seed) / static_cast<float>(0xFFFFFFFF)) * 2.0f - 1.0f;
        }

        // pink noise approximation using filtered white noise
        float z0 = 0, z1 = 0, z2 = 0;
        float pink()
        {
            float w = white();
            z0 = 0.99886f * z0 + w * 0.0555179f;
            z1 = 0.99332f * z1 + w * 0.0750759f;
            z2 = 0.96900f * z2 + w * 0.1538520f;
            return (z0 + z1 + z2 + w * 0.5362f) * 0.2f;
        }
    };

    // main synthesis engine
    class synthesizer
    {
    public:
        void initialize(int sample_rate = tuning::sample_rate)
        {
            m_sample_rate = static_cast<float>(sample_rate);

            // initialize oscillators for each cylinder pair (v12 = 6 firing events per revolution)
            m_firing_oscillators.resize(tuning::cylinder_count / 2);
            for (auto& osc : m_firing_oscillators)
                osc.reset();

            // harmonic oscillators
            m_harmonic_oscillators.resize(8); // 4 main + 4 for detuning
            for (auto& osc : m_harmonic_oscillators)
                osc.reset();

            // exhaust filter - let more through for presence
            m_exhaust_filter_1.set_lowpass(800.0f, 0.6f, m_sample_rate);
            m_exhaust_filter_2.set_bandpass(200.0f, 0.8f, m_sample_rate);  // low-mid body
            m_exhaust_filter_3.set_bandpass(1200.0f, 1.0f, m_sample_rate); // presence/bark

            // output - gentle rolloff, keep the bite
            m_output_lowpass.set_lowpass(8000.0f, 0.707f, m_sample_rate);

            m_initialized = true;
            m_debug.initialized = true;
        }

        void set_parameters(float rpm, float throttle, float load, float boost_pressure = 0.0f)
        {
            m_target_rpm      = std::clamp(rpm, tuning::idle_rpm, tuning::max_rpm);
            m_target_throttle = std::clamp(throttle, 0.0f, 1.0f);
            m_target_load     = std::clamp(load, 0.0f, 1.0f);
            m_boost_pressure  = std::clamp(boost_pressure, 0.0f, 2.0f);

            // update debug data
            m_debug.rpm      = m_target_rpm;
            m_debug.throttle = m_target_throttle;
            m_debug.load     = m_target_load;
            m_debug.boost    = m_boost_pressure;
        }

        void generate(float* output_buffer, int num_samples, bool stereo = true)
        {
            m_debug.generate_calls++;
            m_debug.samples_generated += num_samples;

            if (!m_initialized)
            {
                // fill with silence
                int total_samples = stereo ? num_samples * 2 : num_samples;
                for (int i = 0; i < total_samples; i++)
                    output_buffer[i] = 0.0f;
                return;
            }

            float dt = 1.0f / m_sample_rate;

            // accumulators for debug levels
            float combustion_sum = 0.0f;
            float exhaust_sum    = 0.0f;
            float turbo_sum      = 0.0f;
            float output_sum     = 0.0f;
            float output_peak    = 0.0f;

            for (int i = 0; i < num_samples; i++)
            {
                // smooth parameter transitions
                float smooth_factor = 1.0f - expf(-tuning::throttle_response * dt);
                m_current_rpm      += (m_target_rpm - m_current_rpm) * smooth_factor;
                m_current_throttle += (m_target_throttle - m_current_throttle) * smooth_factor;
                m_current_load     += (m_target_load - m_current_load) * smooth_factor;

                // rpm normalized 0-1
                float rpm_normalized = std::clamp((m_current_rpm - tuning::idle_rpm) / (tuning::redline_rpm - tuning::idle_rpm), 0.0f, 1.0f);

                // fundamental frequency scales with rpm
                float fundamental = tuning::idle_base_freq + rpm_normalized * (tuning::redline_base_freq - tuning::idle_base_freq);
                m_debug.firing_freq = fundamental;

                // how much sawtooth vs sine - more aggressive at high rpm
                float saw_mix = tuning::saw_amount_idle + rpm_normalized * (tuning::saw_amount_redline - tuning::saw_amount_idle);
                saw_mix *= (0.7f + m_current_throttle * 0.3f); // more grit with throttle

                // === engine tone: 6 harmonics with character ===
                float combustion = 0.0f;
                float gains[] = {
                    tuning::harmonic_1_gain,
                    tuning::harmonic_2_gain,
                    tuning::harmonic_3_gain,
                    tuning::harmonic_4_gain,
                    tuning::harmonic_5_gain,
                    tuning::harmonic_6_gain
                };

                for (int h = 0; h < 6; h++)
                {
                    float freq = fundamental * (h + 1);
                    m_harmonic_oscillators[h].set_frequency(freq, m_sample_rate);

                    // get phase and compute both waveforms without double-advancing
                    float p = m_harmonic_oscillators[h].phase;
                    float sine_part = sinf(p * 2.0f * 3.14159265f);
                    float saw_part  = 2.0f * p - 1.0f;
                    m_harmonic_oscillators[h].advance();

                    // mix sine and sawtooth for character
                    float harmonic = sine_part * (1.0f - saw_mix) + saw_part * saw_mix;

                    // higher harmonics get boosted at high rpm for that scream
                    float rpm_boost = 1.0f;
                    if (h >= 3)
                        rpm_boost = 1.0f + rpm_normalized * m_current_throttle * 0.6f;

                    combustion += harmonic * gains[h] * rpm_boost;
                }

                // normalize (6 harmonics can sum high)
                combustion *= 0.3f;

                // === exhaust character ===
                float exhaust = 0.0f;

                // filtered noise for body
                float noise_raw = m_noise.pink() * tuning::exhaust_noise;
                exhaust += m_exhaust_filter_1.process(noise_raw) * 0.5f;  // low rumble
                exhaust += m_exhaust_filter_2.process(noise_raw) * 0.3f;  // mid body
                exhaust += m_exhaust_filter_3.process(noise_raw) * (0.2f + rpm_normalized * 0.3f); // bark at high rpm

                // crackle and pop on deceleration (low throttle, high rpm)
                if (m_current_throttle < 0.2f && rpm_normalized > 0.3f)
                {
                    float crackle_chance = tuning::crackle_amount * rpm_normalized * (1.0f - m_current_throttle * 5.0f);
                    if (m_noise.white() > (1.0f - crackle_chance * 0.1f))
                    {
                        exhaust += m_noise.white() * crackle_chance * 2.0f;
                    }
                }

                exhaust *= (0.6f + m_current_throttle * 0.4f);

                // === turbo whine ===
                float turbo = 0.0f;
                if (m_boost_pressure > 0.1f)
                {
                    float turbo_freq = 2000.0f + m_boost_pressure * 3000.0f;
                    m_turbo_oscillator.set_frequency(turbo_freq, m_sample_rate);
                    turbo = m_turbo_oscillator.sine() * m_boost_pressure * 0.06f;
                }

                // === final mix ===
                float output = combustion * 0.75f + exhaust * 0.2f + turbo;

                // volume envelope
                float volume = tuning::idle_volume + rpm_normalized * (tuning::redline_volume - tuning::idle_volume);
                volume *= (0.6f + m_current_throttle * 0.4f);
                output *= volume;

                // gentle lowpass - don't over-filter
                output = m_output_lowpass.process(output);

                // saturation for warmth and punch
                output = tanhf(output * 2.5f) * 0.75f;

                // accumulate debug info
                combustion_sum += combustion * combustion;
                exhaust_sum    += exhaust * exhaust;
                turbo_sum      += turbo * turbo;
                output_sum     += output * output;
                if (fabsf(output) > output_peak) output_peak = fabsf(output);

                // store in waveform buffer (decimated for visualization)
                if ((i % 4) == 0)
                {
                    m_debug.waveform[m_debug.waveform_write_pos] = output;
                    m_debug.waveform_write_pos = (m_debug.waveform_write_pos + 1) % debug_data::waveform_size;
                }

                // output
                if (stereo)
                {
                    // slight stereo variation for width
                    float stereo_var = m_noise.white() * 0.02f;
                    output_buffer[i * 2]     = output * (1.0f + stereo_var);
                    output_buffer[i * 2 + 1] = output * (1.0f - stereo_var);
                }
                else
                {
                    output_buffer[i] = output;
                }
            }

            // compute rms levels for debug display
            float inv_n = 1.0f / static_cast<float>(num_samples);
            m_debug.combustion_level = sqrtf(combustion_sum * inv_n);
            m_debug.exhaust_level    = sqrtf(exhaust_sum * inv_n);
            m_debug.induction_level  = 0.0f; // removed from synthesis
            m_debug.mechanical_level = 0.0f; // removed from synthesis
            m_debug.turbo_level      = sqrtf(turbo_sum * inv_n);
            m_debug.output_level     = sqrtf(output_sum * inv_n);
            m_debug.output_peak      = output_peak;
        }

        void reset()
        {
            m_current_rpm      = tuning::idle_rpm;
            m_current_throttle = 0.0f;
            m_current_load     = 0.0f;

            for (auto& osc : m_firing_oscillators)
                osc.reset();
            for (auto& osc : m_harmonic_oscillators)
                osc.reset();

            m_exhaust_filter_1.reset();
            m_exhaust_filter_2.reset();
            m_exhaust_filter_3.reset();
            m_induction_filter.reset();
            m_mechanical_filter.reset();
            m_turbo_filter.reset();
            m_output_lowpass.reset();
        }

        bool is_initialized() const { return m_initialized; }
        const debug_data& get_debug() const { return m_debug; }

    private:
        bool  m_initialized     = false;
        float m_sample_rate     = tuning::sample_rate;

        // target parameters (from car physics)
        float m_target_rpm      = tuning::idle_rpm;
        float m_target_throttle = 0.0f;
        float m_target_load     = 0.0f;
        float m_boost_pressure  = 0.0f;

        // smoothed current values
        float m_current_rpm      = tuning::idle_rpm;
        float m_current_throttle = 0.0f;
        float m_current_load     = 0.0f;

        // oscillators
        std::vector<oscillator> m_firing_oscillators;
        std::vector<oscillator> m_harmonic_oscillators;
        oscillator              m_turbo_oscillator;

        // filters
        biquad_filter m_exhaust_filter_1;
        biquad_filter m_exhaust_filter_2;
        biquad_filter m_exhaust_filter_3;
        biquad_filter m_induction_filter;
        biquad_filter m_mechanical_filter;
        biquad_filter m_turbo_filter;
        biquad_filter m_output_lowpass;

        // noise
        noise_generator m_noise;

        // debug
        debug_data m_debug;
    };

    // global synthesizer instance
    inline synthesizer& get_synthesizer()
    {
        static synthesizer instance;
        return instance;
    }

    inline void initialize(int sample_rate = tuning::sample_rate)
    {
        get_synthesizer().initialize(sample_rate);
    }

    inline void set_parameters(float rpm, float throttle, float load, float boost = 0.0f)
    {
        get_synthesizer().set_parameters(rpm, throttle, load, boost);
    }

    inline void generate(float* buffer, int num_samples, bool stereo = true)
    {
        get_synthesizer().generate(buffer, num_samples, stereo);
    }

    inline void reset()
    {
        get_synthesizer().reset();
    }

    inline const debug_data& get_debug()
    {
        return get_synthesizer().get_debug();
    }

    // debug visualization window
    inline void debug_window()
    {
        if (!ImGui::Begin("Engine Sound Synthesis", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::End();
            return;
        }

        const debug_data& dbg = get_debug();

        // status
        ImGui::TextColored(dbg.initialized ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
            "Status: %s", dbg.initialized ? "Initialized" : "NOT Initialized");

        ImGui::Text("Generate calls: %llu", dbg.generate_calls);
        ImGui::Text("Samples generated: %llu", dbg.samples_generated);

        ImGui::Separator();

        // input parameters
        ImGui::Text("Input Parameters:");
        ImGui::Text("  RPM: %.0f", dbg.rpm);
        ImGui::Text("  Throttle: %.1f%%", dbg.throttle * 100.0f);
        ImGui::Text("  Load: %.1f%%", dbg.load * 100.0f);
        ImGui::Text("  Boost: %.2f bar", dbg.boost);
        ImGui::Text("  Fundamental: %.1f Hz", dbg.firing_freq);

        ImGui::Separator();

        // component levels with bars
        ImGui::Text("Component Levels (RMS):");

        auto draw_level_bar = [](const char* label, float level, ImU32 color)
        {
            ImGui::Text("  %s:", label);
            ImGui::SameLine(120);

            ImVec2 pos = ImGui::GetCursorScreenPos();
            float bar_width = 200.0f;
            float bar_height = 14.0f;
            float fill = std::clamp(level * 5.0f, 0.0f, 1.0f); // scale for visibility

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(pos, ImVec2(pos.x + bar_width, pos.y + bar_height), IM_COL32(40, 40, 40, 255));
            draw_list->AddRectFilled(pos, ImVec2(pos.x + bar_width * fill, pos.y + bar_height), color);
            draw_list->AddRect(pos, ImVec2(pos.x + bar_width, pos.y + bar_height), IM_COL32(80, 80, 80, 255));

            ImGui::Dummy(ImVec2(bar_width, bar_height));
            ImGui::SameLine();
            ImGui::Text("%.4f", level);
        };

        draw_level_bar("Combustion", dbg.combustion_level, IM_COL32(255, 100, 100, 255));
        draw_level_bar("Exhaust", dbg.exhaust_level, IM_COL32(255, 180, 100, 255));
        draw_level_bar("Induction", dbg.induction_level, IM_COL32(100, 200, 255, 255));
        draw_level_bar("Mechanical", dbg.mechanical_level, IM_COL32(200, 200, 100, 255));
        draw_level_bar("Turbo", dbg.turbo_level, IM_COL32(100, 255, 200, 255));

        ImGui::Separator();

        // output levels
        ImGui::Text("Output:");
        draw_level_bar("RMS", dbg.output_level, IM_COL32(100, 255, 100, 255));
        draw_level_bar("Peak", dbg.output_peak, IM_COL32(255, 255, 100, 255));

        ImGui::Separator();

        // waveform visualization
        ImGui::Text("Waveform:");
        {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float width = 400.0f;
            float height = 100.0f;
            float center_y = pos.y + height * 0.5f;

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            // background
            draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(20, 20, 25, 255));

            // center line
            draw_list->AddLine(ImVec2(pos.x, center_y), ImVec2(pos.x + width, center_y), IM_COL32(60, 60, 60, 255));

            // waveform
            int start_idx = dbg.waveform_write_pos;
            float x_step = width / static_cast<float>(debug_data::waveform_size);

            for (int i = 0; i < debug_data::waveform_size - 1; i++)
            {
                int idx0 = (start_idx + i) % debug_data::waveform_size;
                int idx1 = (start_idx + i + 1) % debug_data::waveform_size;

                float x0 = pos.x + i * x_step;
                float x1 = pos.x + (i + 1) * x_step;
                float y0 = center_y - dbg.waveform[idx0] * height * 0.45f;
                float y1 = center_y - dbg.waveform[idx1] * height * 0.45f;

                draw_list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(100, 200, 100, 255), 1.5f);
            }

            // border
            draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(80, 80, 80, 255));

            ImGui::Dummy(ImVec2(width, height));
        }

        // scale labels
        ImGui::Text("Scale: +/- 1.0 (vertical)  |  ~%d samples (horizontal)", debug_data::waveform_size * 4);

        ImGui::End();
    }
}
