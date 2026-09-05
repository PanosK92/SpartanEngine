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

#include "pch.h"
#include "CarEngineSoundSynthesis.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace engine_sound
{
    namespace
    {
        constexpr float pi     = 3.14159265358979f;
        constexpr float two_pi = 6.28318530717959f;

        // exhaust gas cools on its way out, so sound travels slower the further it gets from the head
        constexpr float sound_speed_primary   = 520.0f;
        constexpr float sound_speed_collector = 450.0f;
        constexpr float sound_speed_tailpipe  = 400.0f;

        // slow moving state such as filter cutoffs is refreshed every this many samples
        constexpr int control_interval = 64;

        // how far past its nominal width a blowdown pulse is followed, in widths, until it has died out
        constexpr float pulse_window = 2.2f;

        float clamp01(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        float lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        float smoothstep(float edge0, float edge1, float x)
        {
            float t = clamp01((x - edge0) / (edge1 - edge0));
            return t * t * (3.0f - 2.0f * t);
        }

        struct rng
        {
            std::uint32_t state = 0x9E3779B9u;

            void seed(std::uint32_t value)
            {
                state = value ? value : 0x9E3779B9u;
            }

            float uniform()
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                return static_cast<float>(state) * (1.0f / 4294967296.0f);
            }

            float bipolar()
            {
                return uniform() * 2.0f - 1.0f;
            }
        };

        struct one_pole
        {
            float z = 0.0f;
            float a = 1.0f;

            void set_cutoff(float hz, float sample_rate)
            {
                a = 1.0f - expf(-two_pi * hz / sample_rate);
            }

            float process(float x)
            {
                z += a * (x - z);
                return z;
            }

            void reset(float value = 0.0f)
            {
                z = value;
            }
        };

        struct dc_blocker
        {
            float x1 = 0.0f;
            float y1 = 0.0f;
            float r  = 0.9965f;

            float process(float x)
            {
                float y = x - x1 + r * y1;
                x1 = x;
                y1 = y;
                return y;
            }

            void reset()
            {
                x1 = y1 = 0.0f;
            }
        };

        // rbj biquad, transposed direct form two
        struct biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
            float s1 = 0.0f, s2 = 0.0f;

            void set_lowpass(float f, float q, float sample_rate)
            {
                float w0 = two_pi * std::clamp(f, 10.0f, sample_rate * 0.45f) / sample_rate;
                float c  = cosf(w0);
                float alpha = sinf(w0) / (2.0f * std::max(q, 0.1f));
                float a0 = 1.0f + alpha;
                b0 = (1.0f - c) * 0.5f / a0;
                b1 = (1.0f - c) / a0;
                b2 = b0;
                a1 = -2.0f * c / a0;
                a2 = (1.0f - alpha) / a0;
            }

            void set_highpass(float f, float q, float sample_rate)
            {
                float w0 = two_pi * std::clamp(f, 10.0f, sample_rate * 0.45f) / sample_rate;
                float c  = cosf(w0);
                float alpha = sinf(w0) / (2.0f * std::max(q, 0.1f));
                float a0 = 1.0f + alpha;
                b0 = (1.0f + c) * 0.5f / a0;
                b1 = -(1.0f + c) / a0;
                b2 = b0;
                a1 = -2.0f * c / a0;
                a2 = (1.0f - alpha) / a0;
            }

            void set_bandpass(float f, float q, float sample_rate)
            {
                float w0 = two_pi * std::clamp(f, 10.0f, sample_rate * 0.45f) / sample_rate;
                float c  = cosf(w0);
                float alpha = sinf(w0) / (2.0f * std::max(q, 0.1f));
                float a0 = 1.0f + alpha;
                b0 = alpha / a0;
                b1 = 0.0f;
                b2 = -alpha / a0;
                a1 = -2.0f * c / a0;
                a2 = (1.0f - alpha) / a0;
            }

            void set_peak(float f, float q, float gain_db, float sample_rate)
            {
                float w0 = two_pi * std::clamp(f, 10.0f, sample_rate * 0.45f) / sample_rate;
                float c  = cosf(w0);
                float alpha = sinf(w0) / (2.0f * std::max(q, 0.1f));
                float a  = powf(10.0f, gain_db / 40.0f);
                float a0 = 1.0f + alpha / a;
                b0 = (1.0f + alpha * a) / a0;
                b1 = -2.0f * c / a0;
                b2 = (1.0f - alpha * a) / a0;
                a1 = -2.0f * c / a0;
                a2 = (1.0f - alpha / a) / a0;
            }

            float process(float x)
            {
                float y = b0 * x + s1;
                s1 = b1 * x - a1 * y + s2;
                s2 = b2 * x - a2 * y;
                return y;
            }

            void reset()
            {
                s1 = s2 = 0.0f;
            }
        };

        struct delay_line
        {
            std::vector<float> buffer;
            int mask      = 0;
            int write_pos = 0;

            void resize(int max_delay)
            {
                int size = 16;
                while (size < max_delay + 4)
                {
                    size <<= 1;
                }
                buffer.assign(static_cast<size_t>(size), 0.0f);
                mask      = size - 1;
                write_pos = 0;
            }

            void write(float value)
            {
                buffer[static_cast<size_t>(write_pos)] = value;
                write_pos = (write_pos + 1) & mask;
            }

            float read(float delay) const
            {
                delay = std::clamp(delay, 1.0f, static_cast<float>(mask - 1));
                int whole  = static_cast<int>(delay);
                float frac = delay - static_cast<float>(whole);
                int i0 = (write_pos - whole) & mask;
                int i1 = (i0 - 1) & mask;
                float s0 = buffer[static_cast<size_t>(i0)];
                float s1 = buffer[static_cast<size_t>(i1)];
                return s0 + (s1 - s0) * frac;
            }

            void clear()
            {
                std::fill(buffer.begin(), buffer.end(), 0.0f);
                write_pos = 0;
            }
        };

        // a pipe as a feedback comb, the round trip is the delay and the end reflections set the sign,
        // negative feedback is a runner closed at the valve and open at the collector, quarter wave,
        // positive feedback is a pipe open at both ends, half wave
        struct pipe
        {
            delay_line line;
            float round_trip = 64.0f;
            float feedback   = 0.0f;
            one_pole loss;

            void configure(float length_m, float sound_speed, float feedback_gain, float loss_hz, float sample_rate)
            {
                round_trip = std::max(4.0f, 2.0f * length_m / sound_speed * sample_rate);
                line.resize(static_cast<int>(round_trip) + 8);
                feedback = feedback_gain;
                loss.set_cutoff(loss_hz, sample_rate);
            }

            // returns what arrives at the far end
            float process(float input)
            {
                float returned = line.read(round_trip);
                float arriving = line.read(round_trip * 0.5f);
                line.write(input + feedback * loss.process(returned));
                return arriving;
            }

            void reset()
            {
                line.clear();
                loss.reset();
            }
        };

        struct muffler
        {
            biquad chamber[3];
            float  chamber_gain[3] = {};
            biquad absorb;
            float  through = 1.0f;

            void configure(float level, float displacement_l, float sample_rate)
            {
                // chambers ring low and inharmonic, bigger engines get bigger cans
                float base = 85.0f / sqrtf(std::max(displacement_l, 0.5f) / 2.0f);
                base = std::clamp(base, 40.0f, 140.0f);
                const float ratios[3] = { 1.0f, 2.35f, 3.9f };
                const float gains[3]  = { 1.0f, 0.6f, 0.35f };
                for (int i = 0; i < 3; i++)
                {
                    chamber[i].set_bandpass(base * ratios[i], 1.2f + 1.5f * level, sample_rate);
                    chamber_gain[i] = gains[i] * lerp(0.1f, 0.55f, level);
                }

                // absorption eats the crack, a straight pipe keeps it
                float cutoff = lerp(9000.0f, 1300.0f, powf(level, 0.7f));
                absorb.set_lowpass(cutoff, 0.55f, sample_rate);
                through = lerp(1.0f, 0.4f, level);
            }

            float process(float x)
            {
                float body = 0.0f;
                for (int i = 0; i < 3; i++)
                {
                    body += chamber[i].process(x) * chamber_gain[i];
                }
                return absorb.process(x * through + body);
            }

            void reset()
            {
                for (int i = 0; i < 3; i++)
                {
                    chamber[i].reset();
                }
                absorb.reset();
            }
        };

        struct cylinder
        {
            int   bank        = 0;
            float fire_angle  = 0.0f;
            float imbalance   = 1.0f;
            // crank degrees since the exhaust valve opened, huge means idle
            float pulse_phase = 1.0e9f;
            float pulse_amp   = 0.0f;
            float pulse_sharp = 2.0f;
            float rasp_amp    = 0.0f;
            pipe  primary;
        };

        struct exhaust_bank
        {
            pipe       collector;
            muffler    can;
            pipe       tailpipe;
            dc_blocker dc;
            // afterfire and overrun pops are injected here, at the collector
            float pop_env   = 0.0f;
            float pop_decay = 0.99f;

            void reset()
            {
                collector.reset();
                can.reset();
                tailpipe.reset();
                dc.reset();
                pop_env = 0.0f;
            }
        };

        // everything derived from the spec, rebuilt whole when the config changes
        struct engine_model
        {
            engine_config config;
            float sample_rate = 48000.0f;
            std::vector<cylinder> cylinders;
            std::vector<exhaust_bank> banks;
            int   next_event      = 0;
            float crank_angle     = 0.0f;
            float pulse_width_deg = 120.0f;
            float pulse_energy    = 1.0f;
            float sharpness_base  = 2.0f;
            float rasp_base       = 0.3f;
            float openness        = 0.1f;
            float redline_factor  = 0.0f;
            bool  odd_fire        = false;

            void build(const engine_config& in_config, float in_sample_rate)
            {
                config      = in_config;
                sample_rate = in_sample_rate;

                int n     = std::clamp(config.cylinder_count, 1, tuning::max_cylinders);
                int nb    = std::clamp(config.bank_count, 1, 2);
                float spacing = 720.0f / static_cast<float>(n);
                float beta    = config.bank_angle_deg;

                // only the classic ninety degree v6 and the v twin keep their uneven crank timing,
                // every other v engine offsets its pins to fire evenly
                odd_fire = nb == 2 && ((n == 6 && fabsf(beta - 90.0f) < 15.0f) || n == 2);
                float odd_offset = 0.0f;
                if (odd_fire)
                {
                    odd_offset = beta - spacing * std::round(beta / spacing);
                }

                float cc_per_cylinder = config.displacement_l * 1000.0f / static_cast<float>(n);
                pulse_energy   = std::clamp(0.35f + 0.75f * (cc_per_cylinder / 500.0f), 0.4f, 1.6f);
                pulse_energy  *= sqrtf(6.0f / static_cast<float>(n));
                float cr       = clamp01((config.compression_ratio - 8.0f) / 6.0f);
                sharpness_base = 2.0f + 2.5f * cr + 1.0f * config.engine_stage;
                float bore_stroke = config.bore_mm / std::max(config.stroke_mm, 1.0f);
                pulse_width_deg   = 140.0f - 40.0f * clamp01((bore_stroke - 0.85f) / 0.5f);
                redline_factor    = clamp01((config.redline_rpm - 6000.0f) / 4000.0f);
                rasp_base         = 0.25f + 0.25f * redline_factor + 0.2f * config.engine_stage;
                openness          = std::clamp(1.1f - config.muffler_level, 0.1f, 1.0f);

                float primary_loss_hz = 3500.0f + 3500.0f * redline_factor;
                float primary_feedback = 0.5f;

                rng seed_rng;
                seed_rng.seed(0xC0FFEE00u + static_cast<std::uint32_t>(n * 131 + nb));

                cylinders.clear();
                cylinders.resize(static_cast<size_t>(n));
                for (int k = 0; k < n; k++)
                {
                    int cylinder_index = config.firing_order[k];
                    if (cylinder_index < 0 || cylinder_index >= n)
                    {
                        cylinder_index = k;
                    }
                    int bank = std::clamp(config.cylinder_bank[cylinder_index], 0, nb - 1);

                    cylinder& c  = cylinders[static_cast<size_t>(k)];
                    c.bank       = bank;
                    c.fire_angle = static_cast<float>(k) * spacing;
                    if (odd_fire && bank == 1)
                    {
                        c.fire_angle += odd_offset;
                    }
                    // manufacturing spread, no two cylinders breathe alike
                    c.fire_angle += 0.6f * seed_rng.bipolar();
                    c.fire_angle  = fmodf(c.fire_angle + 720.0f, 720.0f);
                    c.imbalance   = 1.0f + 0.07f * seed_rng.bipolar();

                    float length = config.primary_length_m * (1.0f + 0.04f * seed_rng.bipolar());
                    c.primary.configure(length, sound_speed_primary, -primary_feedback, primary_loss_hz, sample_rate);
                }
                std::sort(
                    cylinders.begin(),
                    cylinders.end(),
                    [](const cylinder& a, const cylinder& b)
                    {
                        return a.fire_angle < b.fire_angle;
                    }
                );

                banks.clear();
                banks.resize(static_cast<size_t>(nb));
                for (int b = 0; b < nb; b++)
                {
                    exhaust_bank& bank = banks[static_cast<size_t>(b)];
                    float collector_loss = lerp(4000.0f, 2200.0f, config.muffler_level);
                    float collector_len  = config.collector_length_m * (1.0f + 0.03f * static_cast<float>(b));
                    bank.collector.configure(collector_len, sound_speed_collector, 0.35f, collector_loss, sample_rate);
                    bank.can.configure(config.muffler_level, config.displacement_l, sample_rate);
                    bank.tailpipe.configure(config.tailpipe_length_m, sound_speed_tailpipe, 0.28f, 3500.0f, sample_rate);
                    bank.pop_decay = expf(-1.0f / ((0.005f + 0.004f * openness) * sample_rate));
                }

                next_event  = 0;
                crank_angle = 0.0f;
            }

            void reset()
            {
                for (cylinder& c : cylinders)
                {
                    c.pulse_phase = 1.0e9f;
                    c.primary.reset();
                }
                for (exhaust_bank& b : banks)
                {
                    b.reset();
                }
                next_event  = 0;
                crank_angle = 0.0f;
            }
        };

        // the blowdown as a gamma pulse, fast rise, slow fall, a shallow rarefaction behind it
        float blowdown_shape(float x, float sharp)
        {
            const float peak_at = 0.12f;
            float u = x / peak_at;
            float p = powf(u, sharp) * expf(sharp * (1.0f - u));
            float r = 0.0f;
            if (x > 0.45f)
            {
                float v = (x - 0.45f) / 0.32f;
                r = v * v * expf(2.0f * (1.0f - v));
            }
            return p - 0.35f * r;
        }

        bool write_wav(const char* path, const float* interleaved, int frames, int sample_rate)
        {
            FILE* file = nullptr;
            fopen_s(&file, path, "wb");
            if (!file)
            {
                return false;
            }

            const std::uint16_t channels    = 2;
            const std::uint16_t bits        = 16;
            const std::uint16_t block_align = channels * sizeof(std::int16_t);
            const std::uint32_t data_bytes  = static_cast<std::uint32_t>(frames) * block_align;
            const std::uint32_t riff_size   = 36 + data_bytes;
            const std::uint32_t byte_rate   = static_cast<std::uint32_t>(sample_rate) * block_align;
            const std::uint32_t format_size = 16;
            const std::uint16_t pcm_format  = 1;

            fwrite("RIFF", 1, 4, file);
            fwrite(&riff_size, 4, 1, file);
            fwrite("WAVE", 1, 4, file);
            fwrite("fmt ", 1, 4, file);
            fwrite(&format_size, 4, 1, file);
            fwrite(&pcm_format, 2, 1, file);
            fwrite(&channels, 2, 1, file);
            fwrite(&sample_rate, 4, 1, file);
            fwrite(&byte_rate, 4, 1, file);
            fwrite(&block_align, 2, 1, file);
            fwrite(&bits, 2, 1, file);
            fwrite("data", 1, 4, file);
            fwrite(&data_bytes, 4, 1, file);

            for (int i = 0; i < frames * 2; i++)
            {
                float sample = std::clamp(interleaved[i], -1.0f, 1.0f);
                std::int16_t value = static_cast<std::int16_t>(std::lround(sample * 32767.0f));
                fwrite(&value, sizeof(value), 1, file);
            }

            fclose(file);
            return true;
        }
    }

    engine_config::engine_config()
    {
        for (int i = 0; i < tuning::max_cylinders; i++)
        {
            firing_order[i]  = i;
            cylinder_bank[i] = 0;
        }
    }

    bool engine_config::operator==(const engine_config& other) const
    {
        bool same =
            cylinder_count == other.cylinder_count &&
            bank_count == other.bank_count &&
            bank_angle_deg == other.bank_angle_deg &&
            idle_rpm == other.idle_rpm &&
            redline_rpm == other.redline_rpm &&
            max_rpm == other.max_rpm &&
            displacement_l == other.displacement_l &&
            bore_mm == other.bore_mm &&
            stroke_mm == other.stroke_mm &&
            compression_ratio == other.compression_ratio &&
            primary_length_m == other.primary_length_m &&
            collector_length_m == other.collector_length_m &&
            tailpipe_length_m == other.tailpipe_length_m &&
            muffler_level == other.muffler_level &&
            turbo_enabled == other.turbo_enabled &&
            boost_max_pressure == other.boost_max_pressure &&
            boost_wastegate_rpm == other.boost_wastegate_rpm &&
            engine_stage == other.engine_stage &&
            exhaust_stage == other.exhaust_stage &&
            intake_stage == other.intake_stage &&
            turbo_stage == other.turbo_stage;
        if (!same)
        {
            return false;
        }
        for (int i = 0; i < tuning::max_cylinders; i++)
        {
            if (firing_order[i] != other.firing_order[i] || cylinder_bank[i] != other.cylinder_bank[i])
            {
                return false;
            }
        }
        return true;
    }

    bool engine_config::operator!=(const engine_config& other) const
    {
        return !(*this == other);
    }

    class synthesizer::implementation
    {
    public:
        runtime_params* params = nullptr;

        void initialize(int sample_rate)
        {
            m_sample_rate = static_cast<float>(sample_rate);

            m_rpm_smooth.set_cutoff(25.0f, m_sample_rate);
            m_throttle_smooth.set_cutoff(15.0f, m_sample_rate);
            m_load_smooth.set_cutoff(15.0f, m_sample_rate);
            m_boost_smooth.set_cutoff(10.0f, m_sample_rate);
            m_shaft_smooth.set_cutoff(2.5f, m_sample_rate);
            m_pulse_env_smooth.set_cutoff(400.0f, m_sample_rate);
            for (int i = 0; i < 4; i++)
            {
                m_view_weight_smooth[i].set_cutoff(4.0f, m_sample_rate);
            }
            m_body_cutoff_smooth.set_cutoff(4.0f, m_sample_rate);
            m_cabin_gain_smooth.set_cutoff(4.0f, m_sample_rate);

            m_intake_bp.set_bandpass(500.0f, 0.7f, m_sample_rate);
            m_intake_honk.set_bandpass(220.0f, 4.0f, m_sample_rate);
            m_intake_hp.set_highpass(120.0f, 0.7f, m_sample_rate);
            m_whistle_bp.set_bandpass(3000.0f, 8.0f, m_sample_rate);
            m_bov_bp.set_bandpass(3000.0f, 1.5f, m_sample_rate);
            m_hiss_bp.set_bandpass(4500.0f, 0.8f, m_sample_rate);
            m_tick_hp.set_highpass(3200.0f, 0.7f, m_sample_rate);
            m_body_lp.set_lowpass(16000.0f, 0.6f, m_sample_rate);
            m_cabin_peak.set_peak(90.0f, 1.2f, 0.0f, m_sample_rate);
            m_output_hp.set_highpass(28.0f, 0.7f, m_sample_rate);
            m_width_delay.resize(64);

            m_limiter_release = expf(-1.0f / (0.12f * m_sample_rate));
            m_tick_decay      = expf(-1.0f / (0.0012f * m_sample_rate));
            m_bov_decay       = expf(-1.0f / (0.45f * m_sample_rate));
            m_bov_sweep       = 1.0f - expf(-1.0f / (0.25f * m_sample_rate));

            m_noise.seed(0xA5A5F00Du);
            m_event_rng.seed(0x1234ABCDu);

            {
                std::lock_guard<std::mutex> lock(m_model_mutex);
                if (!m_model)
                {
                    m_model = std::make_unique<engine_model>();
                    m_model->build(engine_config(), m_sample_rate);
                }
            }

            m_initialized.store(true, std::memory_order_release);
            m_debug.initialized = true;
        }

        void configure(const engine_config& config)
        {
            auto model = std::make_unique<engine_model>();
            model->build(config, m_sample_rate);

            std::lock_guard<std::mutex> lock(m_model_mutex);
            m_retired.reset();
            m_pending = std::move(model);
        }

        void set_parameters(float rpm, float throttle, float load, float boost, bool fuel_cut, int gear, bool shifting, listener_view view)
        {
            m_target_rpm.store(std::max(rpm, 0.0f), std::memory_order_relaxed);
            m_target_throttle.store(clamp01(throttle), std::memory_order_relaxed);
            m_target_load.store(clamp01(load), std::memory_order_relaxed);
            m_target_boost.store(std::max(boost, 0.0f), std::memory_order_relaxed);
            m_fuel_cut.store(fuel_cut, std::memory_order_relaxed);
            m_gear.store(gear, std::memory_order_relaxed);
            m_shifting.store(shifting, std::memory_order_relaxed);
            m_view.store(static_cast<int>(view), std::memory_order_relaxed);
        }

        void generate(float* output_buffer, int num_samples, bool stereo)
        {
            const int total = stereo ? num_samples * 2 : num_samples;
            if (!m_initialized.load(std::memory_order_acquire) || num_samples <= 0)
            {
                std::fill(output_buffer, output_buffer + total, 0.0f);
                return;
            }

            swap_in_pending_model();
            engine_model& model = *m_model;
            const engine_config& cfg = model.config;
            const runtime_params p = params ? *params : runtime_params();

            const float target_rpm      = m_target_rpm.load(std::memory_order_relaxed);
            const float target_throttle = m_target_throttle.load(std::memory_order_relaxed);
            const float target_load     = m_target_load.load(std::memory_order_relaxed);
            const float target_boost    = m_target_boost.load(std::memory_order_relaxed);
            const bool  fuel_cut        = m_fuel_cut.load(std::memory_order_relaxed);
            const int   gear            = m_gear.load(std::memory_order_relaxed);
            const listener_view view    = static_cast<listener_view>(m_view.load(std::memory_order_relaxed));

            const float rpm_span    = std::max(cfg.redline_rpm - cfg.idle_rpm, 1.0f);
            const float boost_scale = cfg.turbo_enabled ? 1.0f / std::max(cfg.boost_max_pressure, 0.1f) : 0.0f;
            const float dt          = 1.0f / m_sample_rate;

            // listener weights, exhaust intake turbo mechanical
            float view_target[4] = { 1.0f, 0.45f, 0.55f, 0.3f };
            float body_cutoff_target = 16000.0f;
            float cabin_gain_target  = 0.0f;
            if (view == listener_view::hood)
            {
                const float hood[4] = { 0.75f, 1.0f, 1.0f, 1.0f };
                for (int i = 0; i < 4; i++)
                {
                    view_target[i] = lerp(view_target[i], hood[i], p.cabin_mix);
                }
                body_cutoff_target = lerp(16000.0f, 11000.0f, p.cabin_mix);
            }
            else if (view == listener_view::cabin)
            {
                const float cabin[4] = { 0.55f, 0.6f, 0.7f, 0.55f };
                for (int i = 0; i < 4; i++)
                {
                    view_target[i] = lerp(view_target[i], cabin[i], p.cabin_mix);
                }
                body_cutoff_target = lerp(16000.0f, 2600.0f, p.cabin_mix);
                cabin_gain_target  = 6.0f * p.cabin_mix;
            }

            float sum_exhaust = 0.0f, sum_intake = 0.0f, sum_turbo = 0.0f, sum_mech = 0.0f, sum_pop = 0.0f;
            float sum_out = 0.0f, peak = 0.0f;
            float bank_out[2] = { 0.0f, 0.0f };

            for (int i = 0; i < num_samples; i++)
            {
                // controls
                const float rpm       = m_rpm_smooth.process(target_rpm);
                const float throttle  = m_throttle_smooth.process(target_throttle);
                const float load      = m_load_smooth.process(target_load);
                const float boost     = m_boost_smooth.process(target_boost);
                const float rpm_norm  = clamp01((rpm - cfg.idle_rpm) / rpm_span);
                const float boost_norm = std::clamp(boost * boost_scale, 0.0f, 1.2f);

                if ((m_control_counter++ % control_interval) == 0)
                {
                    update_control(model, p, rpm, rpm_norm, throttle, load, boost_norm, fuel_cut, view_target, body_cutoff_target, cabin_gain_target);
                }

                // crank
                const float deg_per_sample = rpm / 60.0f * 360.0f * dt;
                model.crank_angle += deg_per_sample;
                const int n = static_cast<int>(model.cylinders.size());
                for (int guard = 0; guard < n; guard++)
                {
                    cylinder& c = model.cylinders[static_cast<size_t>(model.next_event)];
                    if (c.fire_angle > model.crank_angle)
                    {
                        break;
                    }
                    fire(model, c, model.crank_angle - c.fire_angle, load, rpm_norm, boost_norm, fuel_cut, p);
                    model.next_event++;
                    if (model.next_event >= n)
                    {
                        model.next_event = 0;
                        model.crank_angle -= 720.0f;
                    }
                }

                // cylinders into their runners
                const float white = m_noise.bipolar();
                float pulse_env = 0.0f;
                int nb = static_cast<int>(model.banks.size());
                float bank_in[2] = { 0.0f, 0.0f };
                for (cylinder& c : model.cylinders)
                {
                    float excitation = 0.0f;
                    if (c.pulse_phase < model.pulse_width_deg * pulse_window)
                    {
                        float x = c.pulse_phase / model.pulse_width_deg;
                        float shape = blowdown_shape(x, c.pulse_sharp);
                        float positive = std::max(shape, 0.0f);
                        excitation = c.pulse_amp * (shape + c.rasp_amp * positive * white);
                        pulse_env += positive * c.pulse_amp;
                        c.pulse_phase += deg_per_sample;
                    }
                    bank_in[c.bank] += c.primary.process(excitation);
                }
                const float pulse_env_smooth = m_pulse_env_smooth.process(pulse_env);

                // per bank collector, muffler, tailpipe
                const float drive = 1.0f + 1.6f * load + 0.6f * boost_norm;
                float exhaust_mono = 0.0f;
                float pop_mono = 0.0f;
                for (int b = 0; b < nb; b++)
                {
                    exhaust_bank& bank = model.banks[static_cast<size_t>(b)];
                    float pop = 0.0f;
                    if (bank.pop_env > 1.0e-4f)
                    {
                        pop = bank.pop_env * (0.7f + 0.7f * m_noise.bipolar());
                        bank.pop_env *= bank.pop_decay;
                    }
                    else
                    {
                        bank.pop_env = 0.0f;
                    }
                    pop_mono += pop;

                    // hot gas piles up into a shock under load, a soft clip gives the crackle
                    float x = tanhf((bank_in[b] + pop) * drive) / drive;
                    x = bank.collector.process(x);
                    x = bank.can.process(x);
                    x = bank.tailpipe.process(x) * 0.65f + x * 0.35f;
                    x = bank.dc.process(x);
                    bank_out[b] = x;
                    exhaust_mono += x;
                }
                if (nb == 1)
                {
                    bank_out[1] = bank_out[0];
                }
                const float exhaust_gain = 0.55f * p.exhaust_level * m_view_weight[0];
                exhaust_mono *= exhaust_gain;

                // induction roar, gulps of air at the firing rate through a throttle plate
                float intake = 0.0f;
                {
                    float breath = m_intake_bp.process(white) * (0.55f + 0.45f * std::min(pulse_env_smooth * 2.0f, 1.0f));
                    breath += m_intake_honk.process(breath) * 0.8f * cfg.intake_stage;
                    breath = m_intake_hp.process(breath);
                    float gain = powf(throttle, 1.4f) * (0.3f + 0.7f * rpm_norm) * (0.25f + 0.75f * cfg.intake_stage);
                    intake = breath * gain * 0.4f * p.intake_level * m_view_weight[1];
                }

                // turbo, whistle from the shaft, blow off hiss on lift, flutter at the wastegate
                float turbo = 0.0f;
                if (cfg.turbo_enabled)
                {
                    float shaft_target = 0.08f + 0.92f * sqrtf(clamp01(boost_norm));
                    shaft_target = std::max(shaft_target, 0.12f + 0.3f * throttle * rpm_norm);
                    float shaft = m_shaft_smooth.process(shaft_target);
                    float freq  = 900.0f + 6500.0f * powf(shaft, 1.6f);
                    m_whistle_phase += two_pi * freq * dt;
                    m_whistle_phase2 += two_pi * freq * 1.985f * dt;
                    if (m_whistle_phase > two_pi)
                    {
                        m_whistle_phase -= two_pi;
                    }
                    if (m_whistle_phase2 > two_pi)
                    {
                        m_whistle_phase2 -= two_pi;
                    }
                    float whistle = sinf(m_whistle_phase) * 0.6f + sinf(m_whistle_phase2) * 0.25f + m_whistle_bp.process(white) * 2.5f;
                    float whistle_gain = powf(shaft, 2.5f) * (0.35f + 0.65f * boost_norm) * (0.5f + 0.5f * cfg.turbo_stage) * (0.4f + 0.6f * throttle);

                    m_flutter_phase += two_pi * 27.0f * dt;
                    if (m_flutter_phase > two_pi)
                    {
                        m_flutter_phase -= two_pi;
                    }
                    float flutter = 1.0f - 0.45f * m_flutter_amount * (0.5f + 0.5f * sinf(m_flutter_phase));
                    float hiss = m_hiss_bp.process(white) * (m_flutter_amount * (0.3f + 0.7f * (1.0f - flutter)) * 0.6f + boost_norm * 0.15f);

                    float bov = 0.0f;
                    if (m_bov_env > 1.0e-4f)
                    {
                        m_bov_freq += (700.0f - m_bov_freq) * m_bov_sweep;
                        bov = m_bov_bp.process(white) * m_bov_env * 3.0f;
                        m_bov_env *= m_bov_decay;
                    }

                    turbo = (whistle * whistle_gain * flutter * 0.12f + hiss * 0.1f + bov * 0.5f) * p.turbo_level * m_view_weight[2];
                }

                // valvetrain ticks and a faint gear whine
                float mech = 0.0f;
                {
                    m_tick_phase += rpm / 60.0f * static_cast<float>(n) * dt;
                    if (m_tick_phase >= 1.0f)
                    {
                        m_tick_phase -= 1.0f;
                        m_tick_env = 0.6f + 0.4f * m_noise.uniform();
                    }
                    float tick = m_tick_hp.process(white) * m_tick_env;
                    m_tick_env *= m_tick_decay;
                    float tick_gain = (0.9f - 0.6f * load) * (1.0f - 0.5f * rpm_norm) * 0.06f;

                    float whine_freq = rpm / 60.0f * (21.0f + 3.0f * static_cast<float>(std::max(gear - 1, 0)));
                    m_whine_phase += two_pi * whine_freq * dt;
                    if (m_whine_phase > two_pi)
                    {
                        m_whine_phase -= two_pi;
                    }
                    float whine = (sinf(m_whine_phase) + 0.4f * sinf(2.0f * m_whine_phase)) * (0.4f + 0.6f * load) * 0.006f * rpm_norm;

                    mech = (tick * tick_gain + whine) * p.mechanical_level * m_view_weight[3];
                }

                // mix, body, limiter
                float center = intake + turbo + mech;
                float left   = bank_out[0] * 0.8f + bank_out[1] * 0.2f;
                float right  = bank_out[1] * 0.8f + bank_out[0] * 0.2f;
                left  = left * exhaust_gain + center;
                right = right * exhaust_gain + center;
                float mono = 0.5f * (left + right);

                mono = m_cabin_peak.process(mono);
                mono = m_body_lp.process(mono);
                mono = m_output_hp.process(mono);
                float side = 0.5f * (left - right);
                side = m_side_lp.process(side);

                // a short cross delay widens the exhaust without smearing the pulses
                m_width_delay.write(side);
                float wide = side * 0.7f + m_width_delay.read(14.0f) * 0.3f;

                float out_l = (mono + wide) * p.master_gain;
                float out_r = (mono - wide) * p.master_gain;

                float loud = std::max(fabsf(out_l), fabsf(out_r));
                m_limiter_env = std::max(loud, m_limiter_env * m_limiter_release);
                float limiter_gain = m_limiter_env > 0.85f ? 0.85f / m_limiter_env : 1.0f;
                out_l = tanhf(out_l * limiter_gain * 1.15f) * 0.87f;
                out_r = tanhf(out_r * limiter_gain * 1.15f) * 0.87f;

                if (stereo)
                {
                    output_buffer[i * 2]     = out_l;
                    output_buffer[i * 2 + 1] = out_r;
                }
                else
                {
                    output_buffer[i] = 0.5f * (out_l + out_r);
                }

                // meters
                float out_mono = 0.5f * (out_l + out_r);
                sum_exhaust += exhaust_mono * exhaust_mono;
                sum_intake  += intake * intake;
                sum_turbo   += turbo * turbo;
                sum_mech    += mech * mech;
                sum_pop     += pop_mono * pop_mono;
                sum_out     += out_mono * out_mono;
                peak = std::max(peak, loud);
                m_debug.waveform[m_debug.waveform_write_pos] = out_mono;
                m_debug.waveform_write_pos = (m_debug.waveform_write_pos + 1) % debug_data::waveform_size;
                m_debug.limiter_gain = limiter_gain;

                if (m_dump_active && m_dump_progress < m_dump_total)
                {
                    m_dump_buffer[static_cast<size_t>(m_dump_progress) * 2]     = out_l;
                    m_dump_buffer[static_cast<size_t>(m_dump_progress) * 2 + 1] = out_r;
                    m_dump_progress++;
                    m_debug.dump_progress = m_dump_progress;
                    if (m_dump_progress >= m_dump_total)
                    {
                        m_dump_active = false;
                        m_debug.dump_ready = true;
                    }
                }
            }

            const float inv_n = 1.0f / static_cast<float>(num_samples);
            m_debug.rpm              = m_rpm_smooth.z;
            m_debug.throttle         = m_throttle_smooth.z;
            m_debug.load             = m_load_smooth.z;
            m_debug.boost            = m_boost_smooth.z;
            m_debug.firing_freq      = m_rpm_smooth.z / 60.0f * static_cast<float>(cfg.cylinder_count) * 0.5f;
            m_debug.exhaust_level    = sqrtf(sum_exhaust * inv_n);
            m_debug.intake_level     = sqrtf(sum_intake * inv_n);
            m_debug.turbo_level      = sqrtf(sum_turbo * inv_n);
            m_debug.mechanical_level = sqrtf(sum_mech * inv_n);
            m_debug.pop_level        = sqrtf(sum_pop * inv_n);
            m_debug.output_level     = sqrtf(sum_out * inv_n);
            m_debug.output_peak      = peak;
            m_debug.odd_fire         = model.odd_fire;
            m_debug.generate_calls++;
            m_debug.samples_generated += static_cast<std::uint64_t>(num_samples);
        }

        void reset()
        {
            swap_in_pending_model();
            if (m_model)
            {
                m_model->reset();
            }
            m_rpm_smooth.reset();
            m_throttle_smooth.reset();
            m_load_smooth.reset();
            m_boost_smooth.reset();
            m_shaft_smooth.reset();
            m_pulse_env_smooth.reset();
            m_intake_bp.reset();
            m_intake_honk.reset();
            m_intake_hp.reset();
            m_whistle_bp.reset();
            m_bov_bp.reset();
            m_hiss_bp.reset();
            m_tick_hp.reset();
            m_body_lp.reset();
            m_cabin_peak.reset();
            m_output_hp.reset();
            m_side_lp.reset();
            m_width_delay.clear();
            m_limiter_env    = 0.0f;
            m_tick_env       = 0.0f;
            m_bov_env        = 0.0f;
            m_flutter_amount = 0.0f;
            m_lift_time      = 10.0f;
            m_prev_throttle  = 0.0f;
        }

        bool is_initialized() const
        {
            return m_initialized.load(std::memory_order_acquire);
        }

        const engine_config& get_config() const
        {
            return m_model ? m_model->config : m_default_config;
        }

        const debug_data& get_debug() const
        {
            return m_debug;
        }

        bool begin_dump(float seconds)
        {
            if (m_dump_active || seconds <= 0.0f)
            {
                return false;
            }
            m_dump_total    = static_cast<int>(seconds * m_sample_rate);
            m_dump_progress = 0;
            m_dump_buffer.assign(static_cast<size_t>(m_dump_total) * 2, 0.0f);
            m_debug.dump_total    = m_dump_total;
            m_debug.dump_progress = 0;
            m_debug.dump_ready    = false;
            m_dump_active = true;
            return true;
        }

        bool dump_ready() const
        {
            return m_debug.dump_ready;
        }

        bool save_dump(const char* path)
        {
            if (!m_debug.dump_ready || m_dump_buffer.empty())
            {
                return false;
            }
            bool result = write_wav(path, m_dump_buffer.data(), m_dump_total, static_cast<int>(m_sample_rate));
            m_dump_buffer.clear();
            m_dump_total    = 0;
            m_dump_progress = 0;
            m_debug.dump_total    = 0;
            m_debug.dump_progress = 0;
            m_debug.dump_ready    = false;
            return result;
        }

    private:
        void swap_in_pending_model()
        {
            std::lock_guard<std::mutex> lock(m_model_mutex);
            if (m_pending)
            {
                m_retired = std::move(m_model);
                m_model   = std::move(m_pending);
                if (m_retired)
                {
                    // carry the crank over so the sound does not restart on an upgrade
                    m_model->crank_angle = fmodf(std::max(m_retired->crank_angle, 0.0f), 720.0f);
                    int n = static_cast<int>(m_model->cylinders.size());
                    m_model->next_event = 0;
                    for (int k = 0; k < n; k++)
                    {
                        if (m_model->cylinders[static_cast<size_t>(k)].fire_angle > m_model->crank_angle)
                        {
                            break;
                        }
                        m_model->next_event = (k + 1) % n;
                    }
                    if (m_model->next_event == 0 && n > 0 && m_model->cylinders[0].fire_angle <= m_model->crank_angle)
                    {
                        m_model->crank_angle -= 720.0f;
                    }
                }
            }
        }

        void fire(engine_model& model, cylinder& c, float lead_deg, float load, float rpm_norm, float boost_norm, bool fuel_cut, const runtime_params& p)
        {
            c.pulse_phase = std::max(lead_deg, 0.0f);

            // the limiter drops most sparks, a dropped charge still leaves as a weak puff
            bool cut = fuel_cut && m_event_rng.uniform() < 0.75f;
            float load_amp = 0.18f + 0.82f * load;
            // idle is lumpy, full throttle is steady
            float jitter = 1.0f + 0.10f * m_event_rng.bipolar() * (1.0f - 0.6f * load);
            float boost_amp = 1.0f + 0.5f * boost_norm;
            c.pulse_amp   = cut ? 0.06f * model.pulse_energy : model.pulse_energy * load_amp * boost_amp * c.imbalance * jitter;
            c.pulse_sharp = model.sharpness_base + 1.5f * load + 0.6f * boost_norm;
            c.rasp_amp    = model.rasp_base * p.rasp * (0.25f + 0.75f * load) * (0.4f + 0.6f * rpm_norm);

            // raw charge meeting a hot pipe lights off behind the head
            if (cut && m_event_rng.uniform() < 0.35f)
            {
                trigger_pop(model, c.bank, 0.6f + 0.6f * m_event_rng.uniform());
            }
        }

        void trigger_pop(engine_model& model, int bank, float amplitude)
        {
            exhaust_bank& b = model.banks[static_cast<size_t>(std::clamp(bank, 0, static_cast<int>(model.banks.size()) - 1))];
            b.pop_env = std::max(b.pop_env, amplitude * (0.5f + 0.5f * model.openness));
            m_debug.pops_fired++;
        }

        void update_control(engine_model& model, const runtime_params& p, float rpm, float rpm_norm, float throttle, float load, float boost_norm, bool fuel_cut, const float* view_target, float body_cutoff_target, float cabin_gain_target)
        {
            const engine_config& cfg = model.config;
            const float block_dt = static_cast<float>(control_interval) / m_sample_rate;

            for (int i = 0; i < 4; i++)
            {
                m_view_weight[i] = m_view_weight_smooth[i].process(view_target[i]);
            }
            float cutoff = m_body_cutoff_smooth.process(body_cutoff_target);
            m_body_lp.set_lowpass(cutoff, 0.6f, m_sample_rate);
            float cabin_gain = m_cabin_gain_smooth.process(cabin_gain_target);
            m_cabin_peak.set_peak(90.0f, 1.2f, cabin_gain, m_sample_rate);
            m_side_lp.set_cutoff(std::min(cutoff, 6000.0f), m_sample_rate);

            m_intake_bp.set_bandpass(250.0f + 900.0f * rpm_norm, 0.7f, m_sample_rate);
            m_intake_honk.set_bandpass(160.0f + 140.0f * cfg.intake_stage + 60.0f * rpm_norm, 4.0f, m_sample_rate);

            if (cfg.turbo_enabled)
            {
                float shaft = m_shaft_smooth.z;
                float freq  = 900.0f + 6500.0f * powf(shaft, 1.6f);
                m_whistle_bp.set_bandpass(freq, 8.0f, m_sample_rate);
                m_bov_bp.set_bandpass(m_bov_freq, 1.5f, m_sample_rate);

                // lift off with the manifold pressurised vents the compressor
                if (m_prev_throttle > 0.45f && throttle < 0.2f && boost_norm > 0.3f && m_bov_env < 0.2f)
                {
                    m_bov_env  = boost_norm * (0.6f + 0.4f * cfg.turbo_stage);
                    m_bov_freq = 3200.0f + 800.0f * cfg.turbo_stage;
                }

                bool at_gate = cfg.boost_wastegate_rpm > 0.0f && rpm > cfg.boost_wastegate_rpm * 0.97f && boost_norm > 0.85f && throttle > 0.6f;
                float flutter_target = at_gate ? 1.0f : 0.0f;
                m_flutter_amount += (flutter_target - m_flutter_amount) * std::min(block_dt * 6.0f, 1.0f);
            }

            // overrun, a closed throttle at speed keeps feeding a hot pipe
            if (throttle < 0.1f && m_prev_throttle >= 0.3f)
            {
                m_lift_time = 0.0f;
            }
            m_lift_time += block_dt;
            if (throttle < 0.1f && (fuel_cut || rpm_norm > 0.3f))
            {
                float rate = 7.0f * p.pop_rate * model.openness * smoothstep(0.3f, 0.6f, rpm_norm) * expf(-m_lift_time / 1.2f) * (0.6f + 0.4f * cfg.engine_stage + 0.6f * cfg.exhaust_stage);
                if (m_event_rng.uniform() < rate * block_dt)
                {
                    int bank = static_cast<int>(m_event_rng.uniform() * static_cast<float>(model.banks.size()));
                    trigger_pop(model, bank, 0.8f + 0.8f * m_event_rng.uniform());
                }
            }
            m_prev_throttle = throttle;
            (void)load;
        }

        float m_sample_rate = static_cast<float>(tuning::sample_rate);
        std::atomic<bool> m_initialized { false };
        engine_config m_default_config;

        std::mutex m_model_mutex;
        std::unique_ptr<engine_model> m_model;
        std::unique_ptr<engine_model> m_pending;
        std::unique_ptr<engine_model> m_retired;

        std::atomic<float> m_target_rpm { 0.0f };
        std::atomic<float> m_target_throttle { 0.0f };
        std::atomic<float> m_target_load { 0.0f };
        std::atomic<float> m_target_boost { 0.0f };
        std::atomic<bool>  m_fuel_cut { false };
        std::atomic<int>   m_gear { 1 };
        std::atomic<bool>  m_shifting { false };
        std::atomic<int>   m_view { 0 };

        one_pole m_rpm_smooth;
        one_pole m_throttle_smooth;
        one_pole m_load_smooth;
        one_pole m_boost_smooth;
        one_pole m_shaft_smooth;
        one_pole m_pulse_env_smooth;
        one_pole m_view_weight_smooth[4];
        one_pole m_body_cutoff_smooth;
        one_pole m_cabin_gain_smooth;
        one_pole m_side_lp;
        float    m_view_weight[4] = { 1.0f, 0.45f, 0.55f, 0.3f };
        std::uint32_t m_control_counter = 0;

        biquad m_intake_bp;
        biquad m_intake_honk;
        biquad m_intake_hp;
        biquad m_whistle_bp;
        biquad m_bov_bp;
        biquad m_hiss_bp;
        biquad m_tick_hp;
        biquad m_body_lp;
        biquad m_cabin_peak;
        biquad m_output_hp;
        delay_line m_width_delay;

        float m_whistle_phase  = 0.0f;
        float m_whistle_phase2 = 0.0f;
        float m_flutter_phase  = 0.0f;
        float m_flutter_amount = 0.0f;
        float m_bov_env        = 0.0f;
        float m_bov_freq       = 3000.0f;
        float m_bov_decay      = 0.999f;
        float m_bov_sweep      = 0.001f;
        float m_tick_phase     = 0.0f;
        float m_tick_env       = 0.0f;
        float m_tick_decay     = 0.98f;
        float m_whine_phase    = 0.0f;
        float m_limiter_env    = 0.0f;
        float m_limiter_release = 0.999f;
        float m_lift_time      = 10.0f;
        float m_prev_throttle  = 0.0f;

        rng m_noise;
        rng m_event_rng;

        debug_data m_debug;
        std::vector<float> m_dump_buffer;
        int  m_dump_total    = 0;
        int  m_dump_progress = 0;
        bool m_dump_active   = false;
    };

    synthesizer::synthesizer()
        : m_implementation(std::make_unique<implementation>())
    {
        m_implementation->params = &params;
    }

    synthesizer::~synthesizer() = default;

    void synthesizer::initialize(int sample_rate)
    {
        m_implementation->initialize(sample_rate);
    }

    void synthesizer::configure(const engine_config& config)
    {
        m_implementation->configure(config);
    }

    void synthesizer::set_parameters(float rpm, float throttle, float load, float boost_pressure, bool fuel_cut, int gear, bool shifting, listener_view view)
    {
        m_implementation->set_parameters(rpm, throttle, load, boost_pressure, fuel_cut, gear, shifting, view);
    }

    void synthesizer::generate(float* output_buffer, int num_samples, bool stereo)
    {
        m_implementation->generate(output_buffer, num_samples, stereo);
    }

    void synthesizer::reset()
    {
        m_implementation->reset();
    }

    bool synthesizer::is_initialized() const
    {
        return m_implementation->is_initialized();
    }

    const engine_config& synthesizer::get_config() const
    {
        return m_implementation->get_config();
    }

    const debug_data& synthesizer::get_debug() const
    {
        return m_implementation->get_debug();
    }

    bool synthesizer::begin_dump(float seconds)
    {
        return m_implementation->begin_dump(seconds);
    }

    bool synthesizer::dump_ready() const
    {
        return m_implementation->dump_ready();
    }

    bool synthesizer::save_dump(const char* path)
    {
        return m_implementation->save_dump(path);
    }

    synthesizer& get_synthesizer()
    {
        static synthesizer instance;
        return instance;
    }

    void initialize(int sample_rate)
    {
        get_synthesizer().initialize(sample_rate);
    }

    void configure(const engine_config& config)
    {
        get_synthesizer().configure(config);
    }

    void set_parameters(float rpm, float throttle, float load, float boost, bool fuel_cut, int gear, bool shifting, listener_view view)
    {
        get_synthesizer().set_parameters(rpm, throttle, load, boost, fuel_cut, gear, shifting, view);
    }

    void generate(float* buffer, int num_samples, bool stereo)
    {
        get_synthesizer().generate(buffer, num_samples, stereo);
    }

    void reset()
    {
        get_synthesizer().reset();
    }

    const debug_data& get_debug()
    {
        return get_synthesizer().get_debug();
    }
}
