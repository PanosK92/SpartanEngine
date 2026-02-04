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

//= INCLUDES ===============================
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>
#include "../../editor/ImGui/Source/imgui.h"
//==========================================

namespace engine_sound
{
    constexpr float PI = 3.14159265358979f;
    constexpr float TWO_PI = 6.28318530717959f;

    // v12 engine parameters
    namespace tuning
    {
        constexpr int   cylinder_count    = 12;
        constexpr int   sample_rate       = 48000;
        constexpr float idle_rpm          = 1000.0f;
        constexpr float redline_rpm       = 9250.0f;
        constexpr float max_rpm           = 9500.0f;

        // combustion envelope (fraction of cycle)
        constexpr float combustion_attack  = 0.08f;
        constexpr float combustion_hold    = 0.12f;
        constexpr float combustion_decay   = 0.35f;

        // exhaust resonance peaks (hz), shift with rpm
        constexpr float exhaust_res_1_idle = 120.0f;
        constexpr float exhaust_res_1_high = 280.0f;
        constexpr float exhaust_res_2_idle = 350.0f;
        constexpr float exhaust_res_2_high = 800.0f;
        constexpr float exhaust_res_3_idle = 1200.0f;
        constexpr float exhaust_res_3_high = 2800.0f;

        // layer mix levels
        constexpr float combustion_level   = 0.55f;
        constexpr float exhaust_level      = 0.35f;
        constexpr float mechanical_level   = 0.12f;
        constexpr float induction_level    = 0.05f;

        // overrun crackle
        constexpr float crackle_threshold  = 0.15f;
        constexpr float crackle_intensity  = 0.4f;
        constexpr float throttle_response  = 12.0f;

        // turbocharger
        constexpr float turbo_spool_up     = 2.5f;
        constexpr float turbo_spool_down   = 1.8f;
        constexpr float turbo_min_rpm      = 2500.0f;
        constexpr float turbo_full_rpm     = 6000.0f;

        // compressor whine (hz)
        constexpr float turbo_whine_min    = 4000.0f;
        constexpr float turbo_whine_max    = 14000.0f;

        // flutter/surge
        constexpr float flutter_freq       = 22.0f;
        constexpr float flutter_decay      = 3.0f;

        // wastegate
        constexpr float wastegate_freq     = 800.0f;
        constexpr float wastegate_decay    = 3.0f;

        // turbo mix levels
        constexpr float turbo_whine_level   = 0.06f;
        constexpr float turbo_rumble_level  = 0.03f;
        constexpr float turbo_flutter_level = 0.25f;
        constexpr float wastegate_level     = 0.15f;
    }

    struct debug_data
    {
        float rpm              = 0.0f;
        float throttle         = 0.0f;
        float load             = 0.0f;
        float boost            = 0.0f;
        float firing_freq      = 0.0f;

        float combustion_level = 0.0f;
        float exhaust_level    = 0.0f;
        float induction_level  = 0.0f;
        float mechanical_level = 0.0f;
        float turbo_level      = 0.0f;
        float output_level     = 0.0f;
        float output_peak      = 0.0f;

        static constexpr int waveform_size = 512;
        float waveform[waveform_size]      = {};
        int   waveform_write_pos           = 0;

        uint64_t generate_calls    = 0;
        uint64_t samples_generated = 0;
        bool initialized           = false;
    };

    // state variable filter
    struct svf_filter
    {
        float ic1eq = 0.0f;
        float ic2eq = 0.0f;
        float g     = 0.0f;
        float k     = 0.0f;
        float a1    = 0.0f;
        float a2    = 0.0f;
        float a3    = 0.0f;

        void set_params(float freq, float q, float sample_rate)
        {
            freq = std::clamp(freq, 20.0f, sample_rate * 0.45f);
            q = std::max(q, 0.5f);

            g = tanf(PI * freq / sample_rate);
            k = 1.0f / q;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void process(float input, float& lp, float& bp, float& hp)
        {
            float v3 = input - ic2eq;
            float v1 = a1 * ic1eq + a2 * v3;
            float v2 = ic2eq + a2 * ic1eq + a3 * v3;

            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;

            lp = v2;
            bp = v1;
            hp = input - k * v1 - v2;

            if (fabsf(ic1eq) < 1e-15f) ic1eq = 0.0f;
            if (fabsf(ic2eq) < 1e-15f) ic2eq = 0.0f;
        }

        float lowpass(float input)
        {
            float lp, bp, hp;
            process(input, lp, bp, hp);
            return lp;
        }

        float bandpass(float input)
        {
            float lp, bp, hp;
            process(input, lp, bp, hp);
            return bp;
        }

        float highpass(float input)
        {
            float lp, bp, hp;
            process(input, lp, bp, hp);
            return hp;
        }

        void reset()
        {
            ic1eq = ic2eq = 0.0f;
        }
    };

    // one-pole lowpass filter
    struct one_pole
    {
        float z1 = 0.0f;
        float a0 = 0.0f;
        float b1 = 0.0f;

        void set_cutoff(float freq, float sample_rate)
        {
            b1 = expf(-TWO_PI * freq / sample_rate);
            a0 = 1.0f - b1;
        }

        float process(float input)
        {
            z1 = input * a0 + z1 * b1;
            return z1;
        }

        void reset() { z1 = 0.0f; }
    };

    // dc blocker
    struct dc_blocker
    {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float r  = 0.995f;

        float process(float input)
        {
            float y = input - x1 + r * y1;
            x1 = input;
            y1 = y;
            return y;
        }

        void reset() { x1 = y1 = 0.0f; }
    };

    // noise generator
    struct noise_gen
    {
        uint32_t state = 12345;

        float white()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float)state / (float)0xFFFFFFFF * 2.0f - 1.0f;
        }

        // pink noise (paul kellet approximation)
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        float pink()
        {
            float w = white();
            b0 = 0.99886f * b0 + w * 0.0555179f;
            b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f;
            b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f;
            b5 = -0.7616f * b5 - w * 0.0168980f;
            float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
            b6 = w * 0.115926f;
            return out * 0.11f;
        }
    };

    // cylinder combustion model
    struct cylinder
    {
        float phase         = 0.0f;
        float phase_inc     = 0.0f;
        float firing_offset = 0.0f;
        float pressure      = 0.0f;
        float prev_pressure = 0.0f;
        bool  is_firing     = false;
        float fire_phase    = 0.0f;
        float timing_jitter = 0.0f;
        float intensity_var = 1.0f;

        void init(int index, int total_cylinders)
        {
            // v12 firing order (60-degree intervals)
            static const int firing_order_12[] = {0, 6, 4, 10, 2, 8, 5, 11, 1, 7, 3, 9};
            int order_pos = firing_order_12[index % 12];
            firing_offset = (float)order_pos / (float)total_cylinders;

            // per-cylinder variation
            timing_jitter = ((index * 7 + 3) % 17) / 170.0f - 0.05f;
            intensity_var = 0.95f + ((index * 13 + 5) % 11) / 110.0f;
        }

        void set_rpm(float rpm, float sample_rate)
        {
            float cycles_per_second = rpm / 60.0f / 2.0f;
            phase_inc = cycles_per_second / sample_rate;
        }

        float tick(float load, float rpm_norm)
        {
            phase += phase_inc;
            if (phase >= 1.0f)
            {
                phase -= 1.0f;
                is_firing = false;
            }

            float effective_phase = fmodf(phase + firing_offset + timing_jitter * (1.0f - load * 0.5f), 1.0f);
            float window_end = tuning::combustion_attack + tuning::combustion_hold + tuning::combustion_decay;

            if (effective_phase < window_end)
            {
                is_firing = true;
                fire_phase = effective_phase / window_end;

                // pressure envelope
                float env = 0.0f;
                float t = effective_phase;

                if (t < tuning::combustion_attack)
                {
                    float attack_t = t / tuning::combustion_attack;
                    env = attack_t * attack_t * (3.0f - 2.0f * attack_t);
                }
                else if (t < tuning::combustion_attack + tuning::combustion_hold)
                {
                    float hold_t = (t - tuning::combustion_attack) / tuning::combustion_hold;
                    env = 1.0f - hold_t * 0.1f;
                }
                else
                {
                    float decay_t = (t - tuning::combustion_attack - tuning::combustion_hold) / tuning::combustion_decay;
                    env = expf(-4.0f * decay_t) * (1.0f - decay_t * 0.2f);
                }

                float load_factor = 0.3f + load * 0.7f;
                float rpm_sharpness = 1.0f + rpm_norm * 0.5f;
                env = powf(env, 1.0f / rpm_sharpness);

                prev_pressure = pressure;
                pressure = env * load_factor * intensity_var;
            }
            else
            {
                is_firing = false;
                prev_pressure = pressure;
                pressure *= 0.95f;
            }

            return pressure;
        }

        void reset()
        {
            phase = firing_offset;
            pressure = prev_pressure = 0.0f;
            is_firing = false;
            fire_phase = 0.0f;
        }
    };

    class synthesizer
    {
    public:
        void initialize(int sample_rate = tuning::sample_rate)
        {
            m_sample_rate = (float)sample_rate;

            m_cylinders.resize(tuning::cylinder_count);
            for (int i = 0; i < tuning::cylinder_count; i++)
                m_cylinders[i].init(i, tuning::cylinder_count);

            // exhaust filters
            m_exhaust_res1.set_params(200.0f, 2.0f, m_sample_rate);
            m_exhaust_res2.set_params(500.0f, 1.5f, m_sample_rate);
            m_exhaust_res3.set_params(1500.0f, 1.2f, m_sample_rate);
            m_exhaust_body.set_params(300.0f, 0.7f, m_sample_rate);

            // induction filters
            m_induction_res.set_params(400.0f, 3.0f, m_sample_rate);
            m_induction_body.set_params(150.0f, 0.8f, m_sample_rate);

            // mechanical filters
            m_mechanical_hp.set_params(2000.0f, 0.7f, m_sample_rate);
            m_mechanical_lp.set_params(6000.0f, 0.7f, m_sample_rate);

            // turbo filters
            m_turbo_whine_bp.set_params(8000.0f, 6.0f, m_sample_rate);
            m_turbo_rumble_lp.set_params(300.0f, 0.8f, m_sample_rate);
            m_turbo_flutter_bp.set_params(100.0f, 2.0f, m_sample_rate);
            m_wastegate_bp.set_params(tuning::wastegate_freq, 1.2f, m_sample_rate);

            // output filters
            m_output_hp.set_params(35.0f, 0.7f, m_sample_rate);
            m_output_lp.set_params(12000.0f, 0.7f, m_sample_rate);

            // parameter smoothing
            m_rpm_smooth.set_cutoff(8.0f, m_sample_rate);
            m_throttle_smooth.set_cutoff(15.0f, m_sample_rate);
            m_load_smooth.set_cutoff(10.0f, m_sample_rate);

            m_initialized = true;
            m_debug.initialized = true;
        }

        void set_parameters(float rpm, float throttle, float load, float boost_pressure = 0.0f)
        {
            m_target_rpm      = std::clamp(rpm, tuning::idle_rpm, tuning::max_rpm);
            m_target_throttle = std::clamp(throttle, 0.0f, 1.0f);
            m_target_load     = std::clamp(load, 0.0f, 1.0f);
            m_boost_pressure  = std::clamp(boost_pressure, 0.0f, 2.0f);

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
                int total = stereo ? num_samples * 2 : num_samples;
                for (int i = 0; i < total; i++)
                    output_buffer[i] = 0.0f;
                return;
            }

            float combustion_sum = 0.0f, exhaust_sum = 0.0f;
            float induction_sum = 0.0f, mechanical_sum = 0.0f;
            float turbo_sum = 0.0f, output_sum = 0.0f;
            float output_peak = 0.0f;

            for (int i = 0; i < num_samples; i++)
            {
                float rpm      = m_rpm_smooth.process(m_target_rpm);
                float throttle = m_throttle_smooth.process(m_target_throttle);
                float load     = m_load_smooth.process(m_target_load);

                float rpm_norm = (rpm - tuning::idle_rpm) / (tuning::redline_rpm - tuning::idle_rpm);
                rpm_norm = std::clamp(rpm_norm, 0.0f, 1.0f);

                for (auto& cyl : m_cylinders)
                    cyl.set_rpm(rpm, m_sample_rate);

                // combustion
                float combustion_raw = 0.0f;
                float combustion_derivative = 0.0f;
                int firing_count = 0;

                for (auto& cyl : m_cylinders)
                {
                    float pulse = cyl.tick(load, rpm_norm);
                    combustion_raw += pulse;
                    combustion_derivative += (pulse - cyl.prev_pressure);
                    if (cyl.is_firing) firing_count++;
                }

                combustion_raw /= 3.0f;
                combustion_derivative *= 2.0f;

                float combustion = combustion_raw;

                // asymmetric saturation
                float asym = combustion + 0.3f * combustion * combustion;
                combustion = asym / (1.0f + fabsf(asym) * 0.5f);
                combustion += combustion_derivative * (0.3f + rpm_norm * 0.3f);

                // high rpm harmonics
                if (rpm_norm > 0.5f)
                {
                    float high_rpm_factor = (rpm_norm - 0.5f) * 2.0f;
                    float edge = combustion * combustion * (combustion > 0 ? 1.0f : -1.0f);
                    combustion += edge * high_rpm_factor * 0.2f;
                }

                // exhaust
                float res1_freq = tuning::exhaust_res_1_idle + rpm_norm * (tuning::exhaust_res_1_high - tuning::exhaust_res_1_idle);
                float res2_freq = tuning::exhaust_res_2_idle + rpm_norm * (tuning::exhaust_res_2_high - tuning::exhaust_res_2_idle);
                float res3_freq = tuning::exhaust_res_3_idle + rpm_norm * (tuning::exhaust_res_3_high - tuning::exhaust_res_3_idle);

                float q_mod = 1.5f + rpm_norm * 2.5f;
                m_exhaust_res1.set_params(res1_freq, q_mod * 0.8f, m_sample_rate);
                m_exhaust_res2.set_params(res2_freq, q_mod * 0.6f, m_sample_rate);
                m_exhaust_res3.set_params(res3_freq, q_mod * 0.5f, m_sample_rate);

                float exhaust_noise = m_noise.pink() * (0.15f + throttle * 0.1f);
                float exhaust_input = combustion * 0.8f + exhaust_noise;

                float exhaust = 0.0f;
                exhaust += m_exhaust_res1.bandpass(exhaust_input) * 0.5f;
                exhaust += m_exhaust_res2.bandpass(exhaust_input) * 0.35f;
                exhaust += m_exhaust_res3.bandpass(exhaust_input) * (0.2f + rpm_norm * 0.3f);

                float body_freq = 150.0f + rpm_norm * 200.0f;
                m_exhaust_body.set_params(body_freq, 0.7f, m_sample_rate);
                exhaust += m_exhaust_body.lowpass(exhaust_input) * 0.4f;
                exhaust = tanhf(exhaust * 2.0f);

                // overrun crackle
                float crackle = 0.0f;
                if (throttle < tuning::crackle_threshold && rpm_norm > 0.25f)
                {
                    float crackle_intensity = (1.0f - throttle / tuning::crackle_threshold) * rpm_norm;

                    if (m_noise.white() > (0.998f - crackle_intensity * 0.015f))
                    {
                        m_crackle_env = 1.0f;
                        m_crackle_freq = 80.0f + m_noise.white() * 60.0f;
                    }

                    if (m_crackle_env > 0.01f)
                    {
                        float pop = m_noise.white() * m_crackle_env;
                        m_crackle_filter.set_params(m_crackle_freq, 1.5f, m_sample_rate);
                        crackle = m_crackle_filter.bandpass(pop) * tuning::crackle_intensity;
                        m_crackle_env *= 0.95f;
                    }
                }

                // induction
                float induction = 0.0f;
                if (throttle > 0.05f)
                {
                    float intake_pulse = combustion_raw * combustion_raw;

                    m_induction_body.set_params(60.0f + rpm_norm * 80.0f, 0.5f, m_sample_rate);
                    float intake_body = m_induction_body.lowpass(intake_pulse) * throttle;

                    float turb = m_noise.pink() * 0.1f * throttle * (0.3f + combustion_raw * 0.7f);
                    m_induction_res.set_params(100.0f + rpm_norm * 150.0f, 0.6f, m_sample_rate);
                    turb = m_induction_res.lowpass(turb);

                    induction = intake_body * 0.7f + turb * 0.3f;
                    induction *= throttle * 0.5f;
                }

                // mechanical noise
                float mechanical = 0.0f;
                {
                    float valve_tick = combustion_derivative * combustion_derivative * 4.0f;

                    float chain_rattle = m_noise.white() * (0.3f + valve_tick * 0.7f);
                    m_mechanical_hp.set_params(800.0f + rpm_norm * 600.0f, 1.2f, m_sample_rate);
                    chain_rattle = m_mechanical_hp.bandpass(chain_rattle);

                    float gear_freq = 200.0f + rpm * 0.05f;
                    m_mechanical_lp.set_params(gear_freq, 3.0f, m_sample_rate);
                    float gear_whine = m_mechanical_lp.bandpass(m_noise.pink() * 0.3f);

                    mechanical = valve_tick * 0.4f + chain_rattle * 0.4f + gear_whine * 0.2f;
                    mechanical *= (0.3f + rpm_norm * 0.7f);
                    mechanical *= (0.9f + m_noise.white() * 0.1f);
                }

                // turbo
                float turbo = 0.0f;
                {
                    float dt = 1.0f / m_sample_rate;
                    float raw_throttle = m_target_throttle;

                    float rpm_factor = std::clamp((rpm - tuning::turbo_min_rpm) / (tuning::turbo_full_rpm - tuning::turbo_min_rpm), 0.0f, 1.0f);
                    float demand = rpm_factor * throttle * (0.5f + load * 0.5f);
                    m_turbo_target_spool = demand * m_boost_pressure;

                    float prev_spool = m_turbo_spool;

                    // spool dynamics
                    float spool_diff = m_turbo_target_spool - m_turbo_spool;
                    if (spool_diff > 0)
                        m_turbo_spool += spool_diff * tuning::turbo_spool_up * dt;
                    else
                        m_turbo_spool += spool_diff * tuning::turbo_spool_down * dt;
                    m_turbo_spool = std::clamp(m_turbo_spool, 0.0f, 1.0f);

                    float spool_rate = (m_turbo_spool - prev_spool) * m_sample_rate;

                    // flutter on throttle lift
                    float throttle_delta = raw_throttle - m_prev_throttle;
                    if (throttle_delta < -0.08f && m_turbo_spool > 0.25f)
                    {
                        float flutter_strength = m_turbo_spool * fabsf(throttle_delta) * 6.0f;
                        m_turbo_flutter_env = std::max(m_turbo_flutter_env, std::min(flutter_strength, 1.0f));
                    }

                    // wastegate on boost drop
                    float boost_delta = m_boost_pressure - m_prev_boost;
                    if (boost_delta < -0.08f && m_turbo_spool > 0.3f)
                        m_wastegate_env = std::max(m_wastegate_env, fabsf(boost_delta) * 2.5f);

                    m_prev_throttle = raw_throttle;
                    m_prev_boost = m_boost_pressure;

                    // spool whoosh
                    if (m_turbo_spool > 0.02f)
                    {
                        float turbo_noise = m_noise.white() * 0.7f + m_noise.pink() * 0.3f;

                        float whoosh_freq = 300.0f + m_turbo_spool * m_turbo_spool * 2500.0f;
                        float whoosh_q = 0.8f + m_turbo_spool * 1.5f;

                        m_turbo_whine_bp.set_params(whoosh_freq, whoosh_q, m_sample_rate);
                        float whoosh = m_turbo_whine_bp.bandpass(turbo_noise);

                        float air_freq = 1500.0f + m_turbo_spool * 3000.0f;
                        m_turbo_filter.set_params(air_freq, 1.0f, m_sample_rate);
                        float air = m_turbo_filter.bandpass(turbo_noise) * 0.3f;

                        float spool_vol = m_turbo_spool * m_turbo_spool;
                        turbo += (whoosh * 0.7f + air * 0.3f) * spool_vol * tuning::turbo_whine_level * 3.0f;
                    }

                    // spindown whistle
                    if (spool_rate < -0.1f && m_turbo_spool > 0.05f)
                    {
                        float whistle_freq = 2000.0f + m_turbo_spool * 6000.0f;

                        m_turbo_phase += whistle_freq / m_sample_rate;
                        if (m_turbo_phase > 1.0f) m_turbo_phase -= 1.0f;

                        float whistle = sinf(m_turbo_phase * TWO_PI) * 0.6f;
                        whistle += sinf(m_turbo_phase * TWO_PI * 2.0f) * 0.2f;

                        float spindown_intensity = std::min(fabsf(spool_rate) * 2.0f, 1.0f);
                        spindown_intensity *= m_turbo_spool;
                        whistle *= (0.85f + m_noise.white() * 0.15f);

                        turbo += whistle * spindown_intensity * tuning::turbo_whine_level * 1.5f;
                    }

                    // compressor flutter
                    if (m_turbo_flutter_env > 0.01f)
                    {
                        float flutter_freq = tuning::flutter_freq * (0.7f + (1.0f - m_turbo_flutter_env) * 0.8f);
                        m_turbo_flutter_phase += flutter_freq / m_sample_rate;
                        if (m_turbo_flutter_phase > 1.0f) m_turbo_flutter_phase -= 1.0f;

                        float fp = m_turbo_flutter_phase;

                        float pulse = 0.0f;
                        if (fp < 0.12f)
                        {
                            pulse = fp / 0.12f;
                            pulse = pulse * pulse;
                        }
                        else if (fp < 0.4f)
                        {
                            float t = (fp - 0.12f) / 0.28f;
                            pulse = (1.0f - t) * expf(-t * 3.0f);
                        }

                        float flutter_noise = m_noise.white() * pulse;
                        m_turbo_flutter_bp.set_params(250.0f + m_turbo_flutter_env * 200.0f, 1.2f, m_sample_rate);
                        float flutter = m_turbo_flutter_bp.bandpass(flutter_noise);

                        float thump = pulse * sinf(fp * TWO_PI * 1.5f) * 0.4f;
                        flutter = (flutter + thump) * m_turbo_flutter_env;
                        m_turbo_flutter_env *= (1.0f - tuning::flutter_decay * dt);

                        turbo += flutter * tuning::turbo_flutter_level;
                    }

                    // wastegate
                    if (m_wastegate_env > 0.01f)
                    {
                        float wg_noise = m_noise.white();
                        m_wastegate_bp.set_params(600.0f + m_wastegate_env * 600.0f, 0.8f, m_sample_rate);
                        float wastegate = m_wastegate_bp.bandpass(wg_noise);

                        float whoosh = m_noise.pink() * 0.5f;
                        wastegate = (wastegate * 0.6f + whoosh * 0.4f) * m_wastegate_env;
                        m_wastegate_env *= (1.0f - tuning::wastegate_decay * dt);

                        turbo += wastegate * tuning::wastegate_level;
                    }

                    turbo = tanhf(turbo * 1.5f);
                }

                // final mix
                float output = 0.0f;
                output += combustion * tuning::combustion_level;
                output += exhaust * tuning::exhaust_level;
                output += crackle;
                output += induction * tuning::induction_level;
                output += mechanical * tuning::mechanical_level;
                output += turbo;

                // output processing
                output = m_dc_blocker.process(output);
                output = m_output_hp.highpass(output);

                // saturation stage 1
                output = tanhf(output * 1.5f);

                // saturation stage 2
                float drive = 1.5f + throttle * 1.0f + rpm_norm * 0.5f;
                output = output * drive;
                output = output / (1.0f + fabsf(output) * 0.3f);

                // limiter
                output = tanhf(output * 1.2f) * 0.85f;
                output = m_output_lp.lowpass(output);

                float master = 0.7f + throttle * 0.2f + rpm_norm * 0.1f;
                output *= master;

                // debug accumulators
                combustion_sum += combustion * combustion;
                exhaust_sum += exhaust * exhaust;
                induction_sum += induction * induction;
                mechanical_sum += mechanical * mechanical;
                turbo_sum += turbo * turbo;
                output_sum += output * output;
                if (fabsf(output) > output_peak) output_peak = fabsf(output);

                if ((i % 4) == 0)
                {
                    m_debug.waveform[m_debug.waveform_write_pos] = output;
                    m_debug.waveform_write_pos = (m_debug.waveform_write_pos + 1) % debug_data::waveform_size;
                }

                // stereo output
                if (stereo)
                {
                    float stereo_diff = m_noise.white() * 0.015f;
                    float left_bias = 1.0f + stereo_diff;
                    float right_bias = 1.0f - stereo_diff;

                    left_bias += exhaust * 0.05f;
                    right_bias -= exhaust * 0.03f;

                    output_buffer[i * 2]     = output * left_bias;
                    output_buffer[i * 2 + 1] = output * right_bias;
                }
                else
                {
                    output_buffer[i] = output;
                }
            }

            float inv_n = 1.0f / (float)num_samples;
            m_debug.combustion_level = sqrtf(combustion_sum * inv_n);
            m_debug.exhaust_level    = sqrtf(exhaust_sum * inv_n);
            m_debug.induction_level  = sqrtf(induction_sum * inv_n);
            m_debug.mechanical_level = sqrtf(mechanical_sum * inv_n);
            m_debug.turbo_level      = sqrtf(turbo_sum * inv_n);
            m_debug.output_level     = sqrtf(output_sum * inv_n);
            m_debug.output_peak      = output_peak;

            m_debug.firing_freq = m_rpm_smooth.z1 / 60.0f * (tuning::cylinder_count / 2.0f);
        }

        void reset()
        {
            for (auto& cyl : m_cylinders)
                cyl.reset();

            m_exhaust_res1.reset();
            m_exhaust_res2.reset();
            m_exhaust_res3.reset();
            m_exhaust_body.reset();
            m_induction_res.reset();
            m_induction_body.reset();
            m_mechanical_hp.reset();
            m_mechanical_lp.reset();
            m_crackle_filter.reset();
            m_turbo_filter.reset();
            m_output_hp.reset();
            m_output_lp.reset();
            m_dc_blocker.reset();
            m_rpm_smooth.reset();
            m_throttle_smooth.reset();
            m_load_smooth.reset();

            m_crackle_env = 0.0f;

            m_turbo_spool = 0.0f;
            m_turbo_target_spool = 0.0f;
            m_turbo_phase = 0.0f;
            m_turbo_flutter_phase = 0.0f;
            m_turbo_flutter_env = 0.0f;
            m_wastegate_env = 0.0f;
            m_prev_throttle = 0.0f;
            m_prev_boost = 0.0f;

            m_turbo_whine_bp.reset();
            m_turbo_rumble_lp.reset();
            m_turbo_flutter_bp.reset();
            m_wastegate_bp.reset();
        }

        bool is_initialized() const { return m_initialized; }
        const debug_data& get_debug() const { return m_debug; }

    private:
        bool  m_initialized = false;
        float m_sample_rate = tuning::sample_rate;

        float m_target_rpm      = tuning::idle_rpm;
        float m_target_throttle = 0.0f;
        float m_target_load     = 0.0f;
        float m_boost_pressure  = 0.0f;

        std::vector<cylinder> m_cylinders;

        svf_filter m_exhaust_res1, m_exhaust_res2, m_exhaust_res3;
        svf_filter m_exhaust_body;
        svf_filter m_induction_res, m_induction_body;
        svf_filter m_mechanical_hp, m_mechanical_lp;
        svf_filter m_crackle_filter;
        svf_filter m_turbo_filter;
        svf_filter m_output_hp, m_output_lp;

        one_pole m_rpm_smooth;
        one_pole m_throttle_smooth;
        one_pole m_load_smooth;

        dc_blocker m_dc_blocker;
        noise_gen m_noise;

        float m_crackle_env  = 0.0f;
        float m_crackle_freq = 100.0f;

        // turbo state
        float m_turbo_spool          = 0.0f;
        float m_turbo_target_spool   = 0.0f;
        float m_turbo_phase          = 0.0f;
        float m_turbo_flutter_phase  = 0.0f;
        float m_turbo_flutter_env    = 0.0f;
        float m_wastegate_env        = 0.0f;
        float m_prev_throttle        = 0.0f;
        float m_prev_boost           = 0.0f;

        svf_filter m_turbo_whine_bp;
        svf_filter m_turbo_rumble_lp;
        svf_filter m_turbo_flutter_bp;
        svf_filter m_wastegate_bp;

        debug_data m_debug;
    };

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

    inline void debug_window()
    {
        if (!ImGui::Begin("Engine Sound Synthesis", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::End();
            return;
        }

        const debug_data& dbg = get_debug();

        ImGui::TextColored(dbg.initialized ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
            "Status: %s", dbg.initialized ? "Initialized" : "NOT Initialized");

        ImGui::Text("Generate calls: %llu", dbg.generate_calls);
        ImGui::Text("Samples generated: %llu", dbg.samples_generated);

        ImGui::Separator();

        ImGui::Text("Input Parameters:");
        ImGui::Text("  RPM: %.0f", dbg.rpm);
        ImGui::Text("  Throttle: %.1f%%", dbg.throttle * 100.0f);
        ImGui::Text("  Load: %.1f%%", dbg.load * 100.0f);
        ImGui::Text("  Boost: %.2f bar", dbg.boost);
        ImGui::Text("  Firing freq: %.1f Hz", dbg.firing_freq);

        ImGui::Separator();

        ImGui::Text("Component Levels (RMS):");

        auto draw_level_bar = [](const char* label, float level, ImU32 color)
        {
            ImGui::Text("  %s:", label);
            ImGui::SameLine(120);

            ImVec2 pos = ImGui::GetCursorScreenPos();
            float bar_width = 200.0f;
            float bar_height = 14.0f;
            float fill = std::clamp(level * 5.0f, 0.0f, 1.0f);

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

        ImGui::Text("Output:");
        draw_level_bar("RMS", dbg.output_level, IM_COL32(100, 255, 100, 255));
        draw_level_bar("Peak", dbg.output_peak, IM_COL32(255, 255, 100, 255));

        ImGui::Separator();

        ImGui::Text("Waveform:");
        {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float width = 400.0f;
            float height = 100.0f;
            float center_y = pos.y + height * 0.5f;

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(20, 20, 25, 255));
            draw_list->AddLine(ImVec2(pos.x, center_y), ImVec2(pos.x + width, center_y), IM_COL32(60, 60, 60, 255));

            int start_idx = dbg.waveform_write_pos;
            float x_step = width / (float)debug_data::waveform_size;

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

            draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(80, 80, 80, 255));

            ImGui::Dummy(ImVec2(width, height));
        }

        ImGui::Text("Scale: +/- 1.0 (vertical)  |  ~%d samples (horizontal)", debug_data::waveform_size * 4);

        ImGui::End();
    }
}
