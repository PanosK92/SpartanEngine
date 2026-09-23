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
#include <xmmintrin.h>

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

        // valve events in crank degrees after firing tdc, the exhaust pulse is the event clock
        constexpr float exhaust_valve_open_deg = 135.0f;
        constexpr float intake_valve_open_deg  = 350.0f;

        // slow moving state such as filter cutoffs is refreshed every this many samples
        constexpr int control_interval = 64;
        constexpr int oversampling = 2;

        // the collector shaper keeps a fixed ceiling so load reads as level, this sets that ceiling
        constexpr float collector_trim = 0.6f;

        // subnormal filter tails cost ~4x cpu once the engine is silent, flush them for the block
        struct denormal_guard
        {
            unsigned int saved = _mm_getcsr();

            denormal_guard()
            {
                _mm_setcsr(saved | 0x8040);
            }

            ~denormal_guard()
            {
                _mm_setcsr(saved);
            }
        };

        // Blackman-windowed half-band sinc: retain the audible engine harmonics, reject the
        // ultrasonic images before returning to the device rate. Every even offset from the
        // center is zero, so only the center tap and one polyphase branch are evaluated.
        struct decimator
        {
            static constexpr int taps = 95;
            static constexpr int center = (taps - 1) / 2;
            static constexpr int branch = (taps + 1) / 2; // 48 nonzero taps on the output phase
            static constexpr int delay = center / 2 + 1;  // other phase, the center tap is its oldest sample
            alignas(16) float coefficients[branch] = {};
            float center_coefficient = 0.5f;
            // doubled so the newest branch window is always contiguous
            alignas(16) float history_l[branch * 2] = {};
            alignas(16) float history_r[branch * 2] = {};
            float delayed_l[delay] = {};
            float delayed_r[delay] = {};
            int position = 0;
            int delayed_position = 0;
            bool output_phase = false;

            void initialize()
            {
                float full[taps] = {};
                float sum = 0.0f;
                for (int i = 0; i < taps; i++)
                {
                    int offset = i - center;
                    float w = two_pi * static_cast<float>(i) / static_cast<float>(taps - 1);
                    float sinc = offset == 0 ? 0.5f : ((offset & 1) ? sinf(pi * 0.5f * offset) / (pi * offset) : 0.0f);
                    full[i] = sinc * (0.42f - 0.5f * cosf(w) + 0.08f * cosf(2.0f * w));
                    sum += full[i];
                }
                // the window is symmetric, so the newest-first order of the branch doesn't matter
                for (int k = 0; k < branch; k++)
                {
                    coefficients[k] = full[k * 2] / sum;
                }
                center_coefficient = full[center] / sum;
                reset();
            }

            // returns true when an output sample is ready
            bool push(float left, float right, float& out_l, float& out_r)
            {
                output_phase = !output_phase;
                if (!output_phase)
                {
                    delayed_l[delayed_position] = left;
                    delayed_r[delayed_position] = right;
                    delayed_position = (delayed_position + 1) % delay;
                    return false;
                }

                position = position == 0 ? branch - 1 : position - 1;
                history_l[position] = history_l[position + branch] = left;
                history_r[position] = history_r[position + branch] = right;

                const float* window_l = history_l + position;
                const float* window_r = history_r + position;
                __m128 sum_l = _mm_setzero_ps();
                __m128 sum_r = _mm_setzero_ps();
                for (int k = 0; k < branch; k += 4)
                {
                    __m128 c = _mm_load_ps(coefficients + k);
                    sum_l = _mm_add_ps(sum_l, _mm_mul_ps(c, _mm_loadu_ps(window_l + k)));
                    sum_r = _mm_add_ps(sum_r, _mm_mul_ps(c, _mm_loadu_ps(window_r + k)));
                }
                alignas(16) float lanes_l[4];
                alignas(16) float lanes_r[4];
                _mm_store_ps(lanes_l, sum_l);
                _mm_store_ps(lanes_r, sum_r);
                // the next write slot holds the oldest sample of the other phase
                out_l = lanes_l[0] + lanes_l[1] + lanes_l[2] + lanes_l[3] + center_coefficient * delayed_l[delayed_position];
                out_r = lanes_r[0] + lanes_r[1] + lanes_r[2] + lanes_r[3] + center_coefficient * delayed_r[delayed_position];
                return true;
            }

            void reset()
            {
                std::memset(history_l, 0, sizeof(history_l));
                std::memset(history_r, 0, sizeof(history_r));
                std::memset(delayed_l, 0, sizeof(delayed_l));
                std::memset(delayed_r, 0, sizeof(delayed_r));
                position = delayed_position = 0;
                output_phase = false;
            }
        };
        static_assert(decimator::branch % 4 == 0, "the branch is summed four taps at a time");

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
            float base_round_trip = 64.0f; // at the design gas temperature
            float feedback   = 0.0f;
            one_pole loss;
            int quiet_samples = 0;

            void configure(float length_m, float sound_speed, float feedback_gain, float loss_hz, float sample_rate)
            {
                base_round_trip = round_trip = std::max(4.0f, 2.0f * length_m / sound_speed * sample_rate);
                line.resize(static_cast<int>(round_trip) + 8);
                feedback = feedback_gain;
                loss.set_cutoff(loss_hz, sample_rate);
            }

            // hotter gas carries sound faster, so the pipe resonates higher; only ever shortens the line
            void set_speed_scale(float scale)
            {
                round_trip = std::max(4.0f, base_round_trip / std::max(scale, 1.0f));
            }

            // returns what arrives at the far end
            float process(float input)
            {
                float returned = line.read(round_trip);
                float arriving = line.read(round_trip * 0.5f);
                line.write(input + feedback * loss.process(returned));
                return arriving;
            }

            // a pipe that has rung down stops being computed until something is fed into it again
            float process_gated(float input)
            {
                if (input == 0.0f && quiet_samples > line.mask)
                {
                    return 0.0f;
                }
                float arriving = process(input);
                quiet_samples = (fabsf(input) < 1e-9f && fabsf(arriving) < 1e-9f) ? quiet_samples + 1 : 0;
                return arriving;
            }

            void reset()
            {
                line.clear();
                loss.reset();
                quiet_samples = 0;
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
            // samples the whole bank has been silent, the muffler rings on after the pipes
            int quiet_samples = 0;
            int quiet_limit   = 1 << 16;

            void reset()
            {
                collector.reset();
                can.reset();
                tailpipe.reset();
                dc.reset();
                pop_env = 0.0f;
                quiet_samples = 0;
            }
        };

        // everything derived from the spec, rebuilt whole when the config changes
        struct engine_model
        {
            engine_config config;
            float sample_rate = 48000.0f;
            std::vector<cylinder> cylinders;
            std::vector<exhaust_bank> banks;
            pipe intake_runner;
            float intake_resonance_hz = 300.0f;
            int   next_event      = 0;
            float crank_angle     = 0.0f;
            float pulse_width_deg = 120.0f;
            float pulse_energy    = 1.0f;
            float sharpness_base  = 2.0f;
            float rasp_base       = 0.3f;
            float openness        = 0.1f;
            float redline_factor  = 0.0f;
            bool  odd_fire        = false;
            // gas torque of the last sample, drives the crank speed ripple
            float torque_ripple   = 0.0f;
            float torque_mean     = 0.0f;

            void build(const engine_config& in_config, float in_sample_rate)
            {
                config      = in_config;
                sample_rate = in_sample_rate;

                int n     = std::clamp(config.cylinder_count, 1, tuning::max_cylinders);
                int nb    = std::clamp(config.bank_count, 1, 2);
                float spacing = 720.0f / static_cast<float>(n);
                float interval_sum = 0.0f;
                bool explicit_timing = true;
                for (int k = 0; k < n; k++)
                {
                    float interval = config.firing_intervals_deg[k];
                    explicit_timing &= std::isfinite(interval) && interval >= 1.0f;
                    interval_sum += interval;
                }
                explicit_timing &= fabsf(interval_sum - 720.0f) < 0.1f;
                odd_fire = false;
                float event_angle = 0.0f;
                float runner_length = config.intake_runner_length_m > 0.0f ? config.intake_runner_length_m : 0.08f + config.stroke_mm * 0.003f;
                runner_length = std::clamp(runner_length, 0.08f, 2.0f);
                intake_resonance_hz = 343.0f / (4.0f * runner_length);
                intake_runner.configure(runner_length, 343.0f, -0.42f, 4500.0f, sample_rate);

                float cc_per_cylinder = config.displacement_l * 1000.0f / static_cast<float>(n);
                pulse_energy   = std::clamp(0.35f + 0.75f * (cc_per_cylinder / 500.0f), 0.4f, 1.6f);
                pulse_energy  *= sqrtf(6.0f / static_cast<float>(n));
                float cr       = clamp01((config.compression_ratio - 8.0f) / 6.0f);
                sharpness_base = 2.0f + 2.5f * cr + 1.0f * config.engine_stage;
                float bore_stroke = config.bore_mm / std::max(config.stroke_mm, 1.0f);
                pulse_width_deg   = 140.0f - 40.0f * clamp01((bore_stroke - 0.85f) / 0.5f);
                redline_factor    = clamp01((config.redline_rpm - 6000.0f) / 4000.0f);
                rasp_base         = 0.018f + 0.025f * redline_factor + 0.02f * config.engine_stage;
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
                    c.fire_angle = event_angle;
                    float interval = explicit_timing ? config.firing_intervals_deg[k] : spacing;
                    odd_fire |= fabsf(interval - spacing) > 0.1f;
                    event_angle += interval;
                    // A fixed crank geometry with small cylinder pressure differences.
                    c.imbalance = 1.0f + config.combustion_variation * seed_rng.bipolar();

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
                    // Keep the bass cutoff in Hz when the internal sample rate changes.
                    bank.dc.r = expf(-two_pi * 18.0f / sample_rate);
                    bank.pop_decay = expf(-1.0f / ((0.005f + 0.004f * openness) * sample_rate));
                    bank.quiet_limit = static_cast<int>(bank.collector.line.buffer.size() + bank.tailpipe.line.buffer.size() + 0.25f * sample_rate);
                }

                // each power stroke is a half sine of gas torque over the 180 degrees after tdc
                torque_mean = static_cast<float>(n) * (360.0f / pi) / 720.0f;
                torque_ripple = 0.0f;
                next_event  = 0;
                crank_angle = 0.0f;
            }

            void reset()
            {
                intake_runner.reset();
                for (cylinder& c : cylinders)
                {
                    c.pulse_phase = 1.0e9f;
                    c.primary.reset();
                }
                for (exhaust_bank& b : banks)
                {
                    b.reset();
                }
                torque_ripple = 0.0f;
                next_event  = 0;
                crank_angle = 0.0f;
            }

            // the intake breathes ambient air, only the exhaust side heats up
            void set_gas_temperature(float speed_scale)
            {
                for (cylinder& c : cylinders)
                {
                    c.primary.set_speed_scale(speed_scale);
                }
                for (exhaust_bank& b : banks)
                {
                    b.collector.set_speed_scale(speed_scale);
                    b.tailpipe.set_speed_scale(speed_scale);
                }
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
            // Bring the tail to zero continuously instead of cutting the rarefaction.
            return (p - 0.35f * r) * (1.0f - smoothstep(1.6f, pulse_window, x));
        }

        // per sample shapes, tabulated once instead of powf, expf and sinf per cylinder at 96 khz
        struct shape_tables
        {
            static constexpr int pulse_steps = 512;
            static constexpr int sharp_steps = 19;
            static constexpr float sharp_min = 1.0f;
            static constexpr float sharp_max = 10.0f;
            static constexpr int half_sine_steps = 1024;
            float pulse[sharp_steps][pulse_steps + 1] = {};
            float half_sine[half_sine_steps + 1] = {};

            shape_tables()
            {
                for (int s = 0; s < sharp_steps; s++)
                {
                    float sharp = sharp_min + (sharp_max - sharp_min) * static_cast<float>(s) / static_cast<float>(sharp_steps - 1);
                    for (int i = 0; i <= pulse_steps; i++)
                    {
                        pulse[s][i] = blowdown_shape(pulse_window * static_cast<float>(i) / static_cast<float>(pulse_steps), sharp);
                    }
                }
                for (int i = 0; i <= half_sine_steps; i++)
                {
                    half_sine[i] = sinf(pi * static_cast<float>(i) / static_cast<float>(half_sine_steps));
                }
            }

            float blowdown(float x, float sharp) const
            {
                float fx = std::clamp(x / pulse_window, 0.0f, 1.0f) * pulse_steps;
                int ix = std::min(static_cast<int>(fx), pulse_steps - 1);
                float tx = fx - static_cast<float>(ix);
                float fs = (std::clamp(sharp, sharp_min, sharp_max) - sharp_min) / (sharp_max - sharp_min) * (sharp_steps - 1);
                int is = std::min(static_cast<int>(fs), sharp_steps - 2);
                float ts = fs - static_cast<float>(is);
                float a = lerp(pulse[is][ix], pulse[is][ix + 1], tx);
                float b = lerp(pulse[is + 1][ix], pulse[is + 1][ix + 1], tx);
                return lerp(a, b, ts);
            }

            // sin(pi * t) for t in [0, 1]
            float sine_lobe(float t) const
            {
                float f = std::clamp(t, 0.0f, 1.0f) * half_sine_steps;
                int i = std::min(static_cast<int>(f), half_sine_steps - 1);
                return lerp(half_sine[i], half_sine[i + 1], f - static_cast<float>(i));
            }
        };

        const shape_tables& get_shape_tables()
        {
            static const shape_tables tables;
            return tables;
        }

        float wrap_720(float angle)
        {
            while (angle < 0.0f) angle += 720.0f;
            while (angle >= 720.0f) angle -= 720.0f;
            return angle;
        }

        // lock-free single writer snapshot, readers retry if the writer was mid copy
        template <typename T>
        struct seqlock
        {
            std::atomic<std::uint32_t> sequence { 0 };
            T value;

            void store(const T& in)
            {
                sequence.fetch_add(1, std::memory_order_acq_rel);
                std::atomic_thread_fence(std::memory_order_release);
                std::memcpy(static_cast<void*>(&value), &in, sizeof(T));
                std::atomic_thread_fence(std::memory_order_release);
                sequence.fetch_add(1, std::memory_order_release);
            }

            T load() const
            {
                T out;
                for (int attempt = 0; attempt < 64; attempt++)
                {
                    std::uint32_t before = sequence.load(std::memory_order_acquire);
                    if (before & 1u)
                    {
                        continue;
                    }
                    std::memcpy(static_cast<void*>(&out), &value, sizeof(T));
                    std::atomic_thread_fence(std::memory_order_acquire);
                    if (sequence.load(std::memory_order_relaxed) == before)
                    {
                        return out;
                    }
                }
                return out;
            }
        };

        // fixed mix levels, one is the tuned balance
        struct mix_levels
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

        enum command_bits : std::uint32_t
        {
            command_reset = 1u << 0,
            command_start = 1u << 1,
            command_prime = 1u << 2
        };

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
            intake_runner_length_m == other.intake_runner_length_m &&
            intake_valve_duration_deg == other.intake_valve_duration_deg &&
            combustion_variation == other.combustion_variation &&
            crank_inertia == other.crank_inertia &&
            turbo_enabled == other.turbo_enabled &&
            turbo_bypass_valve == other.turbo_bypass_valve &&
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
            if (firing_order[i] != other.firing_order[i] || cylinder_bank[i] != other.cylinder_bank[i] || firing_intervals_deg[i] != other.firing_intervals_deg[i])
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
        void initialize(int sample_rate)
        {
            m_output_sample_rate = static_cast<float>(std::clamp(sample_rate, 8000, 192000));
            m_sample_rate = m_output_sample_rate * oversampling;
            m_decimator.initialize();
            get_shape_tables();

            m_rpm_smooth.set_cutoff(25.0f, m_sample_rate);
            m_throttle_smooth.set_cutoff(15.0f, m_sample_rate);
            m_load_smooth.set_cutoff(15.0f, m_sample_rate);
            m_boost_smooth.set_cutoff(10.0f, m_sample_rate);
            m_gearbox_smooth.set_cutoff(15.0f, m_sample_rate);
            m_overrun_smooth.set_cutoff(3.0f, m_sample_rate / control_interval);
            m_bank_pan_smooth.set_cutoff(4.0f, m_sample_rate / control_interval);
            m_gas_temperature.set_cutoff(0.08f, m_sample_rate / control_interval);
            m_shaft_smooth.set_cutoff(2.5f, m_sample_rate);
            m_pulse_env_smooth.set_cutoff(400.0f, m_sample_rate);
            for (int i = 0; i < 4; i++)
            {
                m_view_weight_smooth[i].set_cutoff(4.0f, m_sample_rate / control_interval);
                m_view_weight_smooth[i].reset(m_view_weight[i]);
            }
            m_body_cutoff_smooth.set_cutoff(4.0f, m_sample_rate / control_interval);
            m_body_cutoff_smooth.reset(16000.0f);
            m_cabin_gain_smooth.set_cutoff(4.0f, m_sample_rate / control_interval);
            m_combustion_noise.set_cutoff(3500.0f, m_sample_rate);
            m_master_smooth.set_cutoff(30.0f, m_sample_rate);

            m_intake_bp.set_bandpass(500.0f, 0.7f, m_sample_rate);
            m_intake_honk.set_bandpass(220.0f, 4.0f, m_sample_rate);
            m_intake_hp.set_highpass(120.0f, 0.7f, m_sample_rate);
            m_whistle_bp.set_bandpass(3000.0f, 8.0f, m_sample_rate);
            m_bov_bp.set_bandpass(3000.0f, 1.5f, m_sample_rate);
            m_hiss_bp.set_bandpass(4500.0f, 0.8f, m_sample_rate);
            m_tick_hp.set_highpass(3200.0f, 0.7f, m_sample_rate);
            m_starter_bp.set_bandpass(950.0f, 0.7f, m_sample_rate);
            m_starter_lp.set_lowpass(1800.0f, 0.7f, m_sample_rate);
            m_body_lp.set_lowpass(16000.0f, 0.6f, m_sample_rate);
            m_cabin_peak.set_peak(90.0f, 1.2f, 0.0f, m_sample_rate);
            m_output_hp.set_highpass(28.0f, 0.7f, m_sample_rate);
            m_width_delay_samples = 14.0f * m_sample_rate / 48000.0f;
            m_width_delay.resize(static_cast<int>(ceilf(m_width_delay_samples)) + 4);

            m_limiter_release = expf(-1.0f / (0.12f * m_sample_rate));
            m_tick_decay      = expf(-1.0f / (0.0012f * m_sample_rate));
            m_bov_decay       = expf(-1.0f / (0.45f * m_sample_rate));
            m_attack_step     = 1.0f / (0.002f * m_sample_rate);
            m_bov_sweep       = 1.0f - expf(-1.0f / (0.4f * m_sample_rate));
            m_surge_decay       = expf(-1.0f / (0.55f * m_sample_rate));
            m_shaft_coast       = 1.0f - expf(-1.0f / (0.38f * m_sample_rate));
            m_surge_rate_slew   = 1.0f - expf(-1.0f / (0.65f * m_sample_rate));

            m_noise.seed(0xA5A5F00Du);
            m_event_rng.seed(0x1234ABCDu);

            {
                std::lock_guard<std::mutex> lock(m_model_mutex);
                engine_config config = m_model ? m_model->config : engine_config();
                m_model = std::make_unique<engine_model>();
                m_model->build(config, m_sample_rate);
                if (m_pending) m_pending->build(m_pending->config, m_sample_rate);
            }

            m_initialized.store(true, std::memory_order_release);
            m_debug.initialized = true;
            m_debug_snapshot.store(m_debug);
        }

        void configure(const engine_config& config)
        {
            auto model = std::make_unique<engine_model>();
            model->build(config, m_sample_rate);
            m_configured = config;

            // the audio thread only ever try_locks this, so holding it here never stalls playback
            std::lock_guard<std::mutex> lock(m_model_mutex);
            m_retired.reset();
            m_pending = std::move(model);
        }

        void set_parameters(float rpm, float throttle, float load, float boost, bool fuel_cut, int gear, bool shifting, listener_view view, float gearbox_rpm, bool overrun, float bank_pan)
        {
            m_target_rpm.store(std::isfinite(rpm) ? std::clamp(rpm, 0.0f, 30000.0f) : 0.0f, std::memory_order_relaxed);
            m_target_throttle.store(std::isfinite(throttle) ? clamp01(throttle) : 0.0f, std::memory_order_relaxed);
            m_target_load.store(std::isfinite(load) ? clamp01(load) : 0.0f, std::memory_order_relaxed);
            m_target_boost.store(std::isfinite(boost) ? std::clamp(boost, 0.0f, 10.0f) : 0.0f, std::memory_order_relaxed);
            m_target_gearbox_rpm.store(std::isfinite(gearbox_rpm) ? std::clamp(fabsf(gearbox_rpm), 0.0f, 30000.0f) : 0.0f, std::memory_order_relaxed);
            m_target_bank_pan.store(std::isfinite(bank_pan) ? std::clamp(bank_pan, -1.0f, 1.0f) : 1.0f, std::memory_order_relaxed);
            m_fuel_cut.store(fuel_cut, std::memory_order_relaxed);
            m_overrun.store(overrun, std::memory_order_relaxed);
            m_gear.store(gear, std::memory_order_relaxed);
            m_shifting.store(shifting, std::memory_order_relaxed);
            m_view.store(static_cast<int>(view), std::memory_order_relaxed);
        }

        void start()
        {
            m_commands.fetch_or(command_start, std::memory_order_release);
        }

        void prime()
        {
            m_commands.fetch_or(command_prime, std::memory_order_release);
        }

        void reset()
        {
            m_commands.fetch_or(command_reset, std::memory_order_release);
        }

        void generate(float* output_buffer, int num_samples, bool stereo)
        {
            if (!output_buffer || num_samples <= 0) return;
            const int total = stereo ? num_samples * 2 : num_samples;
            if (!m_initialized.load(std::memory_order_acquire))
            {
                std::fill(output_buffer, output_buffer + total, 0.0f);
                return;
            }

            denormal_guard denormals;
            const mix_levels p;

            const float target_rpm      = m_target_rpm.load(std::memory_order_relaxed);
            const float target_throttle = m_target_throttle.load(std::memory_order_relaxed);
            const float target_load     = m_target_load.load(std::memory_order_relaxed);
            const float target_boost    = m_target_boost.load(std::memory_order_relaxed);
            const float target_gearbox  = m_target_gearbox_rpm.load(std::memory_order_relaxed);
            const float target_bank_pan = m_target_bank_pan.load(std::memory_order_relaxed);
            const bool  fuel_cut        = m_fuel_cut.load(std::memory_order_relaxed);
            const bool  overrun         = m_overrun.load(std::memory_order_relaxed);
            const bool  shifting        = m_shifting.load(std::memory_order_relaxed);
            const int   gear            = m_gear.load(std::memory_order_relaxed);
            const listener_view view    = static_cast<listener_view>(m_view.load(std::memory_order_relaxed));

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

            apply_commands(target_rpm, target_throttle, target_load, target_boost, target_gearbox, overrun, target_bank_pan, view_target, body_cutoff_target, cabin_gain_target);
            swap_in_pending_model();
            engine_model& model = *m_model;
            const engine_config& cfg = model.config;

            const float rpm_span    = std::max(cfg.redline_rpm - cfg.idle_rpm, 1.0f);
            const float boost_scale = cfg.turbo_enabled ? 1.0f / std::max(cfg.boost_max_pressure, 0.1f) : 0.0f;
            const float dt          = 1.0f / m_sample_rate;
            // Acoustic startup timing: heavier, higher-compression engines crank longer.
            const float crank_duration = std::clamp(0.28f + cfg.crank_inertia * 0.5f + cfg.compression_ratio * 0.012f, 0.35f, 0.75f);
            const float crank_rpm = std::clamp(270.0f - cfg.displacement_l * 9.0f - cfg.compression_ratio * 2.0f, 140.0f, 260.0f);
            const bool dumping = m_dump_active.load(std::memory_order_acquire);

            float sum_exhaust = 0.0f, sum_intake = 0.0f, sum_turbo = 0.0f, sum_mech = 0.0f, sum_pop = 0.0f;
            float sum_out = 0.0f, peak = 0.0f;
            float bank_out[2] = { 0.0f, 0.0f };

            const int internal_samples = num_samples * oversampling;
            const float ramp_step = 1.0f / static_cast<float>(internal_samples);
            int output_index = 0;
            for (int sample = 0; sample < internal_samples; sample++)
            {
                // controls arrive once per block, ramp across it so fast revs don't staircase the pitch
                const float ramp = static_cast<float>(sample + 1) * ramp_step;
                const float block_rpm      = lerp(m_ramp_rpm, target_rpm, ramp);
                const float block_throttle = lerp(m_ramp_throttle, target_throttle, ramp);
                const float block_load     = lerp(m_ramp_load, target_load, ramp);
                const float block_boost    = lerp(m_ramp_boost, target_boost, ramp);
                const float block_gearbox  = lerp(m_ramp_gearbox, target_gearbox, ramp);

                float acoustic_rpm = block_rpm;
                float handover = 1.0f;
                float starter_env = 0.0f;
                float starter_click = 0.0f;
                m_start_combustion = 1.0f;
                if (m_start_time >= 0.0f)
                {
                    float t = m_start_time;
                    float catch_progress = smoothstep(crank_duration, crank_duration + 0.24f, t);
                    handover = smoothstep(crank_duration + 0.3f, crank_duration + 1.1f, t);
                    m_start_combustion = catch_progress;
                    float cranking = crank_rpm * smoothstep(0.0f, 0.09f, t);
                    // Compression slows the starter at each cylinder's TDC.
                    cranking *= 1.0f - 0.14f * sinf(model.crank_angle * pi / 360.0f * cfg.cylinder_count);
                    acoustic_rpm = lerp(lerp(cranking, cfg.idle_rpm * 1.5f, catch_progress), block_rpm, handover);
                    starter_env = smoothstep(0.0f, 0.018f, t) * (1.0f - smoothstep(crank_duration + 0.06f, crank_duration + 0.19f, t));
                    starter_click = smoothstep(0.0f, 0.001f, t) * expf(-t / 0.009f);
                    // once it has caught, a driver already revving it shouldn't wait out the flare
                    m_start_time += catch_progress > 0.5f && block_rpm > cfg.idle_rpm * 1.6f ? dt * 3.0f : dt;
                    if (t >= crank_duration + 1.1f) m_start_time = -1.0f;
                }
                const float rpm       = m_rpm_smooth.process(acoustic_rpm);
                const float throttle  = m_throttle_smooth.process(lerp(0.08f * m_start_combustion, block_throttle, handover));
                const float load      = m_load_smooth.process(lerp(0.28f * m_start_combustion, block_load, handover));
                const float boost     = m_boost_smooth.process(block_boost * handover);
                const float gearbox_rpm = m_gearbox_smooth.process(block_gearbox * handover);
                const float rpm_norm  = clamp01((rpm - cfg.idle_rpm) / rpm_span);
                const float boost_norm = std::clamp(boost * boost_scale, 0.0f, 1.2f);

                if ((m_control_counter++ % control_interval) == 0)
                {
                    update_control(model, p, rpm, rpm_norm, throttle, load, boost_norm, fuel_cut, overrun, shifting, target_bank_pan, view_target, body_cutoff_target, cabin_gain_target);
                }

                const float white = m_noise.bipolar();
                const float combustion_noise = m_combustion_noise.process(white);
                // hot gas piles up into a shock under load; the ceiling stays put, so a harder push is louder
                const float drive = 1.0f + 1.6f * load + 0.6f * boost_norm;
                const float drive_ceiling = collector_trim / tanhf(drive);

                model_frame frame;
                step_model(model, p, rpm, load, rpm_norm, boost_norm, fuel_cut, combustion_noise, drive, drive_ceiling, dt, frame);
                if (m_fading && m_crossfade < 1.0f)
                {
                    // both engines play for a moment, equal power, so an upgrade never drops out
                    // the new pipes start empty, so they run unheard until their first pulses are through
                    model_frame previous;
                    step_model(*m_fading, p, rpm, load, rpm_norm, boost_norm, fuel_cut, combustion_noise, drive, drive_ceiling, dt, previous);
                    const float fade = std::max(m_crossfade, 0.0f);
                    frame.blend(previous, sinf(0.5f * pi * fade), cosf(0.5f * pi * fade));
                    m_crossfade = std::min(m_crossfade + dt / 0.03f, 1.0f);
                }
                const int n = static_cast<int>(model.cylinders.size());
                const float crank_ripple = frame.ripple;
                const float pulse_env_smooth = m_pulse_env_smooth.process(frame.pulse_env);
                bank_out[0] = frame.bank[0];
                bank_out[1] = frame.bank[1];
                const float pop_mono = frame.pops;
                const float exhaust_gain = 0.55f * p.exhaust_level * m_view_weight[0];
                const float exhaust_mono = frame.exhaust * exhaust_gain;

                // induction roar: the charge follows manifold pressure, the plate only adds its hiss
                float intake = 0.0f;
                {
                    float breath = frame.breath;
                    breath += m_intake_honk.process(breath) * (0.6f + 0.8f * cfg.intake_stage);
                    float plate = throttle * sqrtf(throttle);
                    breath += m_intake_bp.process(white) * 0.045f * std::min(pulse_env_smooth, 1.0f) * plate;
                    breath = m_intake_hp.process(breath);
                    float gain = (0.3f + 0.7f * rpm_norm) * (0.25f + 0.75f * cfg.intake_stage);
                    intake = breath * gain * 0.4f * p.intake_level * m_view_weight[1];
                }

                // Compressor whistle, bypass discharge and closed-throttle compressor surge.
                float turbo = 0.0f;
                if (cfg.turbo_enabled)
                {
                    float shaft_target = 0.08f + 0.92f * sqrtf(clamp01(boost_norm));
                    shaft_target = std::max(shaft_target, 0.12f + 0.3f * throttle * rpm_norm);
                    // a surging wheel is being slammed by its own charge and slows down fast
                    shaft_target *= 1.0f - 0.55f * m_surge_env;
                    // Rotor inertia outlasts manifold boost on release.
                    float shaft = shaft_target < m_shaft_smooth.z
                        ? (m_shaft_smooth.z += (shaft_target - m_shaft_smooth.z) * m_shaft_coast)
                        : m_shaft_smooth.process(shaft_target);
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
                    float whistle = sinf(m_whistle_phase) * 0.6f + sinf(m_whistle_phase2) * 0.15f + m_whistle_bp.process(white) * 0.18f;
                    float whistle_gain = powf(shaft, 2.5f) * (0.35f + 0.65f * boost_norm) * (0.5f + 0.5f * cfg.turbo_stage) * (0.4f + 0.6f * throttle);

                    // A wastegate regulates exhaust flow; it does not periodically reverse
                    // compressor flow. Steady boost gets broadband air noise, not a 27 Hz chop.
                    float hiss = m_hiss_bp.process(white) * boost_norm * 0.15f;

                    // compressor surge, the trapped charge stalls the wheel in a train of chops that
                    // slows and falls in pitch as the wheel spins down, the stutututu after a shift
                    float bov   = 0.0f;
                    float surge = 0.0f;
                    if (m_bov_env > 1.0e-4f || m_surge_env > 1.0e-3f)
                    {
                        m_bov_freq += (700.0f - m_bov_freq) * m_bov_sweep;
                        // a valve takes a couple of milliseconds to open, a step would click
                        m_bov_attack = std::min(m_bov_attack + m_attack_step, 1.0f);
                        m_surge_attack = std::min(m_surge_attack + m_attack_step, 1.0f);
                        float vent = m_bov_bp.process(white);
                        bov = vent * m_bov_env * 2.0f * m_bov_attack;
                        m_bov_env *= m_bov_decay;

                        m_surge_phase += m_surge_rate * dt;
                        if (m_surge_phase >= 1.0f)
                        {
                            m_surge_phase -= 1.0f;
                            m_surge_burst  = 0.88f + 0.12f * m_noise.uniform();
                        }
                        m_surge_rate += (7.0f - m_surge_rate) * m_surge_rate_slew;
                        // Rounded, asymmetric air packets: a finite attack and a longer
                        // tail, with zero value/slope at each cycle boundary. Measured
                        // release recordings have broad packets, not impulse-like rattles.
                        float pulse = sinf(pi * powf(m_surge_phase, 0.65f));
                        pulse = pulse * pulse * pulse * pulse;
                        float chop = pulse * m_surge_burst * m_surge_env * m_surge_attack;
                        surge = (m_surge_bp.process(white) * 3.0f + sinf(m_whistle_phase) * 0.18f) * chop;
                        m_surge_env   *= m_surge_decay;
                        whistle_gain  *= 1.0f - 0.6f * m_surge_env;
                    }
                    else
                    {
                        m_surge_env = 0.0f;
                    }

                    turbo = (whistle * whistle_gain * 0.12f + hiss * 0.1f + bov * 0.4f + surge * 0.55f) * p.turbo_level * m_view_weight[2];
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
                    float tick = m_tick_hp.process(white) * m_tick_env * 0.3f;
                    m_tick_env *= m_tick_decay;
                    float tick_gain = (0.9f - 0.6f * load) * (1.0f - 0.5f * rpm_norm) * 0.06f;

                    // gears mesh at the gearbox shaft speed, so the whine dies with the clutch open;
                    // reverse is straight cut and sings loudly even at walking pace
                    float whine = 0.0f;
                    if (gear != 0)
                    {
                        const bool reverse = gear < 0;
                        float teeth = reverse ? 17.0f : 21.0f + 3.0f * static_cast<float>(std::max(gear - 1, 0));
                        m_whine_phase += two_pi * gearbox_rpm / 60.0f * teeth * dt;
                        if (m_whine_phase > two_pi)
                        {
                            m_whine_phase -= two_pi;
                        }
                        float shaft_norm = clamp01(gearbox_rpm / std::max(cfg.redline_rpm, 1.0f));
                        if (reverse)
                        {
                            float tone = sinf(m_whine_phase) + 0.7f * sinf(2.0f * m_whine_phase) + 0.35f * sinf(3.0f * m_whine_phase);
                            whine = tone * (0.5f + 0.5f * load) * 0.03f * std::min(shaft_norm * 4.0f, 1.0f);
                        }
                        else
                        {
                            whine = (sinf(m_whine_phase) + 0.4f * sinf(2.0f * m_whine_phase)) * (0.4f + 0.6f * load) * 0.006f * shaft_norm;
                        }
                    }

                    mech = (tick * tick_gain + whine) * p.mechanical_level * m_view_weight[3];
                    // Measured starts have a broad gear/brush rasp, pulsed by
                    // compression, rather than a single low electronic tone.
                    if (starter_env > 0.0f || starter_click > 1e-5f)
                    {
                        m_starter_active = true;
                        // The pinion disengages as combustion catches; don't sweep
                        // the motor whine up to idle RPM along with the engine.
                        float motor_rpm = std::min(rpm, crank_rpm * 1.1f);
                        m_starter_phase += two_pi * motor_rpm / 60.0f * 72.0f * dt;
                        m_starter_phase = fmodf(m_starter_phase, two_pi);
                        float compression = 0.25f + 0.75f * powf(0.5f + 0.5f * crank_ripple, 0.7f);
                        float phase = m_starter_phase;
                        float gear = 0.62f * sinf(phase) + 0.34f * sinf(2.0f * phase + 0.4f)
                            + 0.38f * sinf(3.0f * phase + 0.8f) + 0.68f * sinf(4.0f * phase + 1.3f)
                            + 0.25f * sinf(5.0f * phase + 1.7f);
                        float brush = m_starter_bp.process(white);
                        float starter = m_starter_lp.process((gear * 0.55f + brush * 1.6f) * compression * starter_env + brush * starter_click * 3.0f);
                        mech += starter * 0.24f * p.mechanical_level * std::max(m_view_weight[3], 0.5f);
                    }
                    else if (m_starter_active)
                    {
                        // both envelopes are already at zero, so clearing the tail is inaudible
                        m_starter_active = false;
                        m_starter_bp.reset();
                        m_starter_lp.reset();
                    }
                }

                // mix, body, limiter; each bank leans toward the side of the car it sits on
                float center = intake + turbo + mech;
                const float near_side = 0.5f + 0.3f * m_bank_pan;
                const float far_side  = 0.5f - 0.3f * m_bank_pan;
                float left   = bank_out[0] * near_side + bank_out[1] * far_side;
                float right  = bank_out[1] * near_side + bank_out[0] * far_side;
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
                float wide = side * 0.7f + m_width_delay.read(m_width_delay_samples) * 0.3f;

                // A stopped engine is silent, including its intake and turbo layers.
                m_model_gain = std::min(m_model_gain + dt / 0.012f, 1.0f);
                float running_gain = smoothstep(40.0f, 300.0f, rpm);
                float gain = m_master_smooth.process(p.master_gain) * std::max(running_gain, starter_env) * m_model_gain;
                float out_l = (mono + wide) * gain;
                float out_r = (mono - wide) * gain;

                float loud = std::max(fabsf(out_l), fabsf(out_r));
                m_limiter_env = std::max(loud, m_limiter_env * m_limiter_release);
                float limiter_gain = m_limiter_env > 0.85f ? 0.85f / m_limiter_env : 1.0f;
                out_l = tanhf(out_l * limiter_gain * 1.15f) * 0.87f;
                out_r = tanhf(out_r * limiter_gain * 1.15f) * 0.87f;

                if (!m_decimator.push(out_l, out_r, out_l, out_r) || output_index >= num_samples)
                {
                    continue;
                }
                const int i = output_index++;

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
                peak = std::max(peak, std::max(fabsf(out_l), fabsf(out_r)));
                m_debug.waveform[m_debug.waveform_write_pos] = out_mono;
                m_debug.waveform_write_pos = (m_debug.waveform_write_pos + 1) % debug_data::waveform_size;
                m_debug.limiter_gain = limiter_gain;

                if (dumping && m_dump_progress < m_dump_total)
                {
                    m_dump_buffer[static_cast<size_t>(m_dump_progress) * 2]     = out_l;
                    m_dump_buffer[static_cast<size_t>(m_dump_progress) * 2 + 1] = out_r;
                    m_dump_progress++;
                    m_debug.dump_total    = m_dump_total;
                    m_debug.dump_progress = m_dump_progress;
                    if (m_dump_progress >= m_dump_total)
                    {
                        m_debug.dump_ready = true;
                        m_dump_active.store(false, std::memory_order_relaxed);
                        m_dump_ready.store(true, std::memory_order_release);
                    }
                }
            }
            // a block without enough internal samples cannot happen, but never hand back garbage
            for (int i = output_index; i < num_samples; i++)
            {
                if (stereo)
                {
                    output_buffer[i * 2] = output_buffer[i * 2 + 1] = 0.0f;
                }
                else
                {
                    output_buffer[i] = 0.0f;
                }
            }
            m_ramp_rpm      = target_rpm;
            m_ramp_throttle = target_throttle;
            m_ramp_load     = target_load;
            m_ramp_boost    = target_boost;
            m_ramp_gearbox  = target_gearbox;

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
            m_debug_snapshot.store(m_debug);
        }

        bool is_initialized() const
        {
            return m_initialized.load(std::memory_order_acquire);
        }

        const engine_config& get_config() const
        {
            return m_configured;
        }

        debug_data get_debug() const
        {
            return m_debug_snapshot.load();
        }

        bool begin_dump(float seconds)
        {
            if (m_dump_active.load(std::memory_order_acquire) || m_dump_ready.load(std::memory_order_acquire) || !std::isfinite(seconds) || seconds <= 0.0f || seconds > 120.0f)
            {
                return false;
            }
            // the audio thread leaves these alone until it sees the release below
            m_dump_total    = static_cast<int>(seconds * m_output_sample_rate);
            m_dump_progress = 0;
            m_dump_buffer.assign(static_cast<size_t>(m_dump_total) * 2, 0.0f);
            m_dump_active.store(true, std::memory_order_release);
            return true;
        }

        bool dump_ready() const
        {
            return m_dump_ready.load(std::memory_order_acquire);
        }

        bool save_dump(const char* path)
        {
            if (!m_dump_ready.load(std::memory_order_acquire) || m_dump_buffer.empty())
            {
                return false;
            }
            bool result = write_wav(path, m_dump_buffer.data(), m_dump_total, static_cast<int>(m_output_sample_rate));
            m_dump_buffer.clear();
            m_dump_total    = 0;
            m_dump_progress = 0;
            m_dump_ready.store(false, std::memory_order_release);
            return result;
        }

    private:
        // what one engine model contributes to a single internal sample
        struct model_frame
        {
            float bank[2]   = { 0.0f, 0.0f };
            float exhaust   = 0.0f;
            float pops      = 0.0f;
            float pulse_env = 0.0f;
            float breath    = 0.0f;
            float ripple    = 0.0f;

            void blend(const model_frame& previous, float gain_new, float gain_previous)
            {
                bank[0]   = bank[0] * gain_new + previous.bank[0] * gain_previous;
                bank[1]   = bank[1] * gain_new + previous.bank[1] * gain_previous;
                exhaust   = exhaust * gain_new + previous.exhaust * gain_previous;
                pops      = pops * gain_new + previous.pops * gain_previous;
                pulse_env = pulse_env * gain_new + previous.pulse_env * gain_previous;
                breath    = breath * gain_new + previous.breath * gain_previous;
            }
        };

        void step_model(engine_model& model, const mix_levels& p, float rpm, float load, float rpm_norm, float boost_norm, bool fuel_cut, float combustion_noise, float drive, float drive_ceiling, float dt, model_frame& frame)
        {
            const engine_config& cfg = model.config;
            const shape_tables& tables = get_shape_tables();
            const int n = static_cast<int>(model.cylinders.size());

            // Small torsional speed ripple from the gas torque of each power stroke, summed at every
            // cylinder's own tdc so odd-fire cranks ripple unevenly; flywheel inertia damps it.
            const float ripple = std::clamp((model.torque_ripple - model.torque_mean) * 1.6f, -1.0f, 1.0f);
            const float ripple_amount = std::min(0.008f, 0.0006f / std::max(cfg.crank_inertia, 0.01f)) * (1.0f - 0.7f * rpm_norm);
            const float deg_per_sample = rpm * (1.0f + ripple * ripple_amount * load) * 6.0f * dt;
            frame.ripple = ripple;
            model.crank_angle += deg_per_sample;
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

            // cylinders into their runners, a cylinder's event angle is its exhaust valve opening
            const float duration = std::clamp(cfg.intake_valve_duration_deg, 120.0f, 320.0f);
            const float inverse_duration = 1.0f / duration;
            float bank_in[2] = { 0.0f, 0.0f };
            float induction = 0.0f;
            float torque = 0.0f;
            for (cylinder& c : model.cylinders)
            {
                float excitation = 0.0f;
                if (c.pulse_phase < model.pulse_width_deg * pulse_window)
                {
                    float x = c.pulse_phase / model.pulse_width_deg;
                    float shape = tables.blowdown(x, c.pulse_sharp);
                    float positive = std::max(shape, 0.0f);
                    excitation = c.pulse_amp * (shape + c.rasp_amp * positive * combustion_noise);
                    frame.pulse_env += positive * c.pulse_amp;
                    c.pulse_phase += deg_per_sample;
                }
                bank_in[c.bank] += c.primary.process_gated(excitation);

                float since_tdc = wrap_720(model.crank_angle - c.fire_angle + exhaust_valve_open_deg);
                if (since_tdc < 180.0f)
                {
                    torque += tables.sine_lobe(since_tdc * (1.0f / 180.0f));
                }
                float intake_angle = wrap_720(since_tdc - intake_valve_open_deg);
                if (intake_angle < duration)
                {
                    float valve = tables.sine_lobe(intake_angle * inverse_duration);
                    induction += valve * valve * c.imbalance;
                }
            }
            model.torque_ripple = torque;

            // airflow follows manifold pressure, so the charge is carried by load and boost, not the pedal
            const float manifold = std::clamp(0.12f + 0.88f * load + 0.35f * boost_norm, 0.0f, 1.5f);
            frame.breath = model.intake_runner.process(induction * sqrtf(6.0f / static_cast<float>(n)) * manifold);

            // per bank collector, muffler, tailpipe
            const int nb = static_cast<int>(model.banks.size());
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
                frame.pops += pop;

                const float in = bank_in[b] + pop;
                if (in == 0.0f && bank.quiet_samples > bank.quiet_limit)
                {
                    frame.bank[b] = 0.0f;
                    continue;
                }
                float x = tanhf(in * drive) * drive_ceiling;
                x = bank.collector.process(x);
                x = bank.can.process(x);
                x = bank.tailpipe.process(x) * 0.65f + x * 0.35f;
                x = bank.dc.process(x);
                bank.quiet_samples = (fabsf(in) < 1e-9f && fabsf(x) < 1e-9f) ? bank.quiet_samples + 1 : 0;
                frame.bank[b] = x;
                frame.exhaust += x;
            }
            if (nb == 1)
            {
                frame.bank[1] = frame.bank[0];
            }
        }

        void apply_commands(float rpm, float throttle, float load, float boost, float gearbox_rpm, bool overrun, float bank_pan, const float* view_target, float body_cutoff_target, float cabin_gain_target)
        {
            const std::uint32_t commands = m_commands.exchange(0, std::memory_order_acquire);
            if (commands == 0)
            {
                return;
            }

            apply_reset();
            if (commands & command_start)
            {
                m_start_time = 0.0f;
                return;
            }
            if (commands & command_prime)
            {
                // settle every smoother on the live controls so nothing sweeps up from zero
                m_rpm_smooth.reset(rpm);
                m_throttle_smooth.reset(throttle);
                m_load_smooth.reset(load);
                m_boost_smooth.reset(boost);
                m_gearbox_smooth.reset(gearbox_rpm);
                m_ramp_rpm      = rpm;
                m_ramp_throttle = throttle;
                m_ramp_load     = load;
                m_ramp_boost    = boost;
                m_ramp_gearbox  = gearbox_rpm;
                for (int i = 0; i < 4; i++)
                {
                    m_view_weight[i] = view_target[i];
                    m_view_weight_smooth[i].reset(view_target[i]);
                }
                m_body_cutoff_smooth.reset(body_cutoff_target);
                m_cabin_gain_smooth.reset(cabin_gain_target);
                m_overrun_level = overrun ? 1.0f : 0.0f;
                m_overrun_smooth.reset(m_overrun_level);
                m_bank_pan = bank_pan;
                m_bank_pan_smooth.reset(bank_pan);
                m_lift_armed = throttle > 0.45f;
                if (m_model)
                {
                    const engine_config& cfg = m_model->config;
                    float rpm_norm = clamp01((rpm - cfg.idle_rpm) / std::max(cfg.redline_rpm - cfg.idle_rpm, 1.0f));
                    m_gas_temperature.reset(clamp01(load) * (0.3f + 0.7f * rpm_norm));
                }
            }
        }

        void apply_reset()
        {
            m_start_time = -1.0f;
            m_start_combustion = 1.0f;
            m_starter_phase = 0.0f;
            m_starter_active = false;
            m_model_gain = 0.0f;
            // a crossfade in flight is dropped, the retired model is freed later off this thread
            if (m_fading)
            {
                m_crossfade = 1.0f;
            }
            swap_in_pending_model();
            if (m_model)
            {
                m_model->reset();
            }
            m_rpm_smooth.reset();
            m_throttle_smooth.reset();
            m_load_smooth.reset();
            m_boost_smooth.reset();
            m_gearbox_smooth.reset();
            m_overrun_smooth.reset();
            m_overrun_level = 0.0f;
            m_bank_pan_smooth.reset(1.0f);
            m_bank_pan = 1.0f;
            m_gas_temperature.reset();
            m_ramp_rpm = m_ramp_throttle = m_ramp_load = m_ramp_boost = m_ramp_gearbox = 0.0f;
            m_shaft_smooth.reset();
            m_pulse_env_smooth.reset();
            m_intake_bp.reset();
            m_intake_honk.reset();
            m_intake_hp.reset();
            m_whistle_bp.reset();
            m_bov_bp.reset();
            m_surge_bp.reset();
            m_hiss_bp.reset();
            m_tick_hp.reset();
            m_starter_bp.reset();
            m_starter_lp.reset();
            m_body_lp.reset();
            m_cabin_peak.reset();
            m_output_hp.reset();
            m_side_lp.reset();
            m_width_delay.clear();
            m_decimator.reset();
            m_combustion_noise.reset();
            m_master_smooth.reset();
            m_noise.seed(0xA5A5F00Du);
            m_event_rng.seed(0x1234ABCDu);
            m_control_counter = 0;
            m_whistle_phase = m_whistle_phase2 = 0.0f;
            m_tick_phase = m_whine_phase = m_surge_phase = 0.0f;
            m_bov_freq = 3000.0f;
            m_surge_rate = 22.0f;
            m_lift_armed = false;
            const float chase[4] = { 1.0f, 0.45f, 0.55f, 0.3f };
            for (int i = 0; i < 4; i++)
            {
                m_view_weight[i] = chase[i];
                m_view_weight_smooth[i].reset(chase[i]);
            }
            m_body_cutoff_smooth.reset(16000.0f);
            m_cabin_gain_smooth.reset();
            m_debug = debug_data();
            m_debug.initialized = is_initialized();
            m_limiter_env    = 0.0f;
            m_tick_env       = 0.0f;
            m_bov_env        = 0.0f;
            m_surge_env      = 0.0f;
            m_surge_burst    = 0.0f;
            m_bov_attack     = 1.0f;
            m_surge_attack   = 1.0f;
            m_prev_shifting  = false;
            m_charge = 0.0f;
            m_turbo_release_time = 1.0f;
            m_lift_time      = 10.0f;
        }

        // audio thread only; never blocks, a busy lock just defers the swap to the next block
        void swap_in_pending_model()
        {
            std::unique_lock<std::mutex> lock(m_model_mutex, std::try_to_lock);
            if (!lock.owns_lock())
            {
                return;
            }
            if (m_fading && m_crossfade >= 1.0f && !m_retired)
            {
                m_retired = std::move(m_fading);
            }
            if (!m_pending || m_fading)
            {
                return;
            }
            // a silent engine swaps outright, a ringing one crossfades into the new pipes
            const bool audible = m_model && m_model_gain > 0.0f;
            if (!audible && m_retired)
            {
                return;
            }
            std::unique_ptr<engine_model> previous = std::move(m_model);
            m_model = std::move(m_pending);
            if (previous)
            {
                // carry the crank over so the sound does not restart on an upgrade
                m_model->crank_angle = fmodf(std::max(previous->crank_angle, 0.0f), 720.0f);
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
            if (audible)
            {
                // 30 ms of pre-roll, then a 30 ms fade
                m_fading = std::move(previous);
                m_crossfade = -1.0f;
            }
            else
            {
                m_retired = std::move(previous);
            }
        }

        void fire(engine_model& model, cylinder& c, float lead_deg, float load, float rpm_norm, float boost_norm, bool fuel_cut, const mix_levels& p)
        {
            c.pulse_phase = std::max(lead_deg, 0.0f);

            // the limiter drops most sparks, a dropped charge still leaves as a weak puff
            bool cut = fuel_cut && m_event_rng.uniform() < 0.75f;
            // closed-throttle overrun cuts fuel: the cylinders only pump, softer, duller and raspier
            const float overrun = m_overrun_level;
            float load_amp = lerp(0.18f + 0.82f * load, 0.1f, overrun);
            // idle is lumpy, full throttle is steady
            float jitter = 1.0f + model.config.combustion_variation * m_event_rng.bipolar() * (1.0f - 0.6f * load);
            float boost_amp = 1.0f + 0.5f * boost_norm;
            float sharp = model.sharpness_base + 1.5f * load + 0.6f * boost_norm;
            c.pulse_amp   = cut ? 0.06f * model.pulse_energy : model.pulse_energy * load_amp * boost_amp * c.imbalance * jitter;
            c.pulse_sharp = lerp(sharp, sharp * 0.55f, overrun);
            c.rasp_amp    = model.rasp_base * p.rasp * (0.25f + 0.75f * load) * (0.4f + 0.6f * rpm_norm) * (1.0f + 1.5f * overrun);
            if (m_start_combustion < 1.0f)
            {
                // Early cycles pump air; individual cylinders begin catching in firing order.
                bool caught = m_start_combustion > 0.0f && m_event_rng.uniform() < m_start_combustion;
                c.pulse_amp *= caught ? (0.45f + 0.55f * m_start_combustion) : 0.035f;
                c.rasp_amp *= m_start_combustion;
            }

            // raw charge meeting a hot pipe lights off behind the head
            if (m_start_combustion >= 1.0f && cut && m_event_rng.uniform() < 0.12f * clamp01(p.pop_rate))
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

        void update_control(engine_model& model, const mix_levels& p, float rpm, float rpm_norm, float throttle, float load, float boost_norm, bool fuel_cut, bool overrun, bool shifting, float bank_pan, const float* view_target, float body_cutoff_target, float cabin_gain_target)
        {
            const engine_config& cfg = model.config;
            const float block_dt = static_cast<float>(control_interval) / m_sample_rate;

            m_overrun_level = m_overrun_smooth.process(overrun ? 1.0f : 0.0f);
            m_bank_pan = m_bank_pan_smooth.process(bank_pan);

            // a slow exhaust gas temperature proxy, hot gas raises every exhaust resonance
            float gas_temperature = m_gas_temperature.process(clamp01(load) * (0.3f + 0.7f * rpm_norm));
            model.set_gas_temperature(sqrtf(1.0f + 0.35f * gas_temperature));

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
            m_intake_honk.set_bandpass(model.intake_resonance_hz * (1.0f + 0.15f * cfg.intake_stage), 2.0f, m_sample_rate);

            // Hysteresis survives smoothed controls: a release crosses these
            // thresholds over many control updates, never in a single step.
            if (throttle > 0.45f) m_lift_armed = true;
            bool lift = m_lift_armed && throttle < 0.2f;
            if (lift)
            {
                m_lift_armed = false;
                m_lift_time = 0.0f;
            }

            if (cfg.turbo_enabled)
            {
                float shaft = m_shaft_smooth.z;
                float freq  = 900.0f + 6500.0f * powf(shaft, 1.6f);
                m_whistle_bp.set_bandpass(freq, 8.0f, m_sample_rate);
                m_bov_bp.set_bandpass(m_bov_freq, 1.5f, m_sample_rate);
                // Each returning flow packet sweeps down through the intake resonance.
                m_surge_bp.set_bandpass(2600.0f + 1800.0f * sqrtf(m_surge_env)
                    + 900.0f * (1.0f - m_surge_phase), 2.0f, m_sample_rate);
                m_turbo_release_time += block_dt;
                // Remember charge upstream of the throttle, which survives the drop
                // in manifold telemetry and the smoothed throttle's release threshold.
                m_charge = std::max(boost_norm, m_charge * expf(-block_dt / 0.2f));
                if (throttle > 0.45f && !shifting && m_turbo_release_time > 0.12f)
                {
                    const float reopen_decay = expf(-block_dt / 0.025f);
                    m_surge_env *= reopen_decay;
                    m_bov_env *= reopen_decay;
                }

                // lift off or a shift with the manifold pressurised vents the compressor
                bool shift_start = shifting && !m_prev_shifting;
                if ((lift || shift_start) && m_charge > 0.25f && m_turbo_release_time > 0.15f)
                {
                    float charge = clamp01(m_charge);
                    m_turbo_release_time = 0.0f;
                    m_charge = 0.0f;
                    m_bov_env     = cfg.turbo_bypass_valve ? charge * (0.35f + 0.25f * cfg.turbo_stage) : 0.0f;
                    m_bov_freq    = 3200.0f + 800.0f * cfg.turbo_stage;
                    m_surge_env   = cfg.turbo_bypass_valve ? 0.0f : charge * (0.7f + 0.3f * cfg.turbo_stage);
                    m_surge_rate  = 16.0f + 6.0f * charge;
                    m_surge_phase = 0.0f;
                    m_surge_burst = 1.0f;
                    // start from a clean filter and ramp in, not from the last event's frozen state
                    m_bov_bp.reset();
                    m_surge_bp.reset();
                    m_bov_attack = m_surge_attack = 0.0f;
                }
                m_prev_shifting = shifting;
            }

            // overrun, a closed throttle at speed keeps feeding a hot pipe
            m_lift_time += block_dt;
            if (throttle < 0.1f && (fuel_cut || m_overrun_level > 0.5f || rpm_norm > 0.3f))
            {
                float rate = 7.0f * p.pop_rate * model.openness * smoothstep(0.3f, 0.6f, rpm_norm) * expf(-m_lift_time / 1.2f) * (0.6f + 0.4f * cfg.engine_stage + 0.6f * cfg.exhaust_stage);
                if (m_event_rng.uniform() < rate * block_dt)
                {
                    int bank = static_cast<int>(m_event_rng.uniform() * static_cast<float>(model.banks.size()));
                    trigger_pop(model, bank, 0.8f + 0.8f * m_event_rng.uniform());
                }
            }
            (void)load;
        }

        float m_sample_rate = static_cast<float>(tuning::sample_rate);
        float m_output_sample_rate = static_cast<float>(tuning::sample_rate);
        float m_start_time = -1.0f;
        float m_start_combustion = 1.0f;
        float m_starter_phase = 0.0f;
        bool m_starter_active = false;
        decimator m_decimator;
        float m_model_gain = 0.0f;
        bool m_lift_armed = false;
        std::atomic<bool> m_initialized { false };
        std::atomic<std::uint32_t> m_commands { 0 };
        engine_config m_configured; // main thread copy, the audio thread swaps models on its own

        // the audio thread owns m_model and m_fading, m_pending and m_retired change hands under the lock
        std::mutex m_model_mutex;
        std::unique_ptr<engine_model> m_model;
        std::unique_ptr<engine_model> m_fading;
        std::unique_ptr<engine_model> m_pending;
        std::unique_ptr<engine_model> m_retired;
        float m_crossfade = 1.0f;

        std::atomic<float> m_target_rpm { 0.0f };
        std::atomic<float> m_target_throttle { 0.0f };
        std::atomic<float> m_target_load { 0.0f };
        std::atomic<float> m_target_boost { 0.0f };
        std::atomic<float> m_target_gearbox_rpm { 0.0f };
        std::atomic<float> m_target_bank_pan { 1.0f };
        std::atomic<bool>  m_fuel_cut { false };
        std::atomic<bool>  m_overrun { false };
        std::atomic<int>   m_gear { 1 };
        std::atomic<bool>  m_shifting { false };
        std::atomic<int>   m_view { 0 };

        // the previous block's targets, each block ramps from these to the new ones
        float m_ramp_rpm      = 0.0f;
        float m_ramp_throttle = 0.0f;
        float m_ramp_load     = 0.0f;
        float m_ramp_boost    = 0.0f;
        float m_ramp_gearbox  = 0.0f;

        one_pole m_rpm_smooth;
        one_pole m_throttle_smooth;
        one_pole m_load_smooth;
        one_pole m_boost_smooth;
        one_pole m_gearbox_smooth;
        one_pole m_overrun_smooth;
        one_pole m_bank_pan_smooth;
        one_pole m_gas_temperature;
        float    m_overrun_level = 0.0f;
        float    m_bank_pan      = 1.0f;
        one_pole m_shaft_smooth;
        one_pole m_pulse_env_smooth;
        one_pole m_combustion_noise;
        one_pole m_master_smooth;
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
        biquad m_surge_bp;
        biquad m_hiss_bp;
        biquad m_tick_hp;
        biquad m_starter_bp;
        biquad m_starter_lp;
        biquad m_body_lp;
        biquad m_cabin_peak;
        biquad m_output_hp;
        delay_line m_width_delay;
        float m_width_delay_samples = 14.0f;

        float m_whistle_phase  = 0.0f;
        float m_whistle_phase2 = 0.0f;
        float m_charge = 0.0f;
        float m_turbo_release_time = 1.0f;
        float m_bov_env        = 0.0f;
        float m_bov_freq       = 3000.0f;
        float m_bov_decay      = 0.999f;
        float m_bov_sweep      = 0.001f;
        float m_surge_env      = 0.0f;
        float m_surge_phase    = 0.0f;
        float m_surge_rate     = 22.0f;
        float m_surge_burst    = 0.0f;
        float m_surge_decay    = 0.999f;
        float m_bov_attack     = 1.0f;
        float m_surge_attack   = 1.0f;
        float m_attack_step    = 0.005f;
        float m_shaft_coast = 0.001f;
        float m_surge_rate_slew   = 0.0001f;
        bool  m_prev_shifting  = false;
        float m_tick_phase     = 0.0f;
        float m_tick_env       = 0.0f;
        float m_tick_decay     = 0.98f;
        float m_whine_phase    = 0.0f;
        float m_limiter_env    = 0.0f;
        float m_limiter_release = 0.999f;
        float m_lift_time      = 10.0f;

        rng m_noise;
        rng m_event_rng;

        // written per sample on the audio thread, readers only ever see the published snapshot
        debug_data m_debug;
        seqlock<debug_data> m_debug_snapshot;
        std::vector<float> m_dump_buffer;
        int  m_dump_total    = 0;
        int  m_dump_progress = 0;
        std::atomic<bool> m_dump_active { false };
        std::atomic<bool> m_dump_ready { false };
    };

    synthesizer::synthesizer()
        : m_implementation(std::make_unique<implementation>())
    {
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

    void synthesizer::set_parameters(float rpm, float throttle, float load, float boost_pressure, bool fuel_cut, int gear, bool shifting, listener_view view, float gearbox_rpm, bool overrun, float bank_pan)
    {
        m_implementation->set_parameters(rpm, throttle, load, boost_pressure, fuel_cut, gear, shifting, view, gearbox_rpm, overrun, bank_pan);
    }

    void synthesizer::generate(float* output_buffer, int num_samples, bool stereo)
    {
        m_implementation->generate(output_buffer, num_samples, stereo);
    }

    void synthesizer::reset()
    {
        m_implementation->reset();
    }

    void synthesizer::start()
    {
        m_implementation->start();
    }

    void synthesizer::prime()
    {
        m_implementation->prime();
    }

    bool synthesizer::is_initialized() const
    {
        return m_implementation->is_initialized();
    }

    const engine_config& synthesizer::get_config() const
    {
        return m_implementation->get_config();
    }

    debug_data synthesizer::get_debug() const
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

    void set_parameters(float rpm, float throttle, float load, float boost, bool fuel_cut, int gear, bool shifting, listener_view view, float gearbox_rpm, bool overrun, float bank_pan)
    {
        get_synthesizer().set_parameters(rpm, throttle, load, boost, fuel_cut, gear, shifting, view, gearbox_rpm, overrun, bank_pan);
    }

    void generate(float* buffer, int num_samples, bool stereo)
    {
        get_synthesizer().generate(buffer, num_samples, stereo);
    }

    void reset()
    {
        get_synthesizer().reset();
    }

    void start()
    {
        get_synthesizer().start();
    }

    void prime()
    {
        get_synthesizer().prime();
    }

    debug_data get_debug()
    {
        return get_synthesizer().get_debug();
    }
}
