/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "CarEngineSoundSynthesis.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <xmmintrin.h>

namespace engine_sound
{
    namespace
    {
        constexpr float pi     = 3.14159265358979f;
        constexpr float two_pi = 6.28318530717959f;

        // burnt gas, close enough to air for the intake too, si units throughout the cylinder model
        constexpr float gas_gamma        = 1.33f;
        constexpr float gas_constant     = 287.0f;
        constexpr float ambient_pressure = 101325.0f;
        constexpr float pascal_per_bar   = 1.0e5f;

        // the exhaust cools on its way out; pipes are laid out for their coldest gas and shorten as it heats up
        constexpr float primary_cold_temperature   = 600.0f;
        constexpr float collector_cold_temperature = 500.0f;
        constexpr float tailpipe_cold_temperature  = 400.0f;

        // the tailpipe's far field is heard from here, and this sound pressure is digital full scale
        constexpr float listener_distance_m = 5.0f;
        constexpr float full_scale_pascal   = 7.0f;
        // a close-coupled catalytic converter sits this far past the header merge
        constexpr float catalyst_distance_m = 0.5f;
        // how much of the outer cylinders' extra distance to the merge a header's pipe routing makes up
        constexpr float header_equalization = 0.65f;
        // difference between the two banks' collector runs, as a fraction of their length
        constexpr float bank_run_mismatch = 0.04f;
        // share of a jet's power that turbulence turns into sound, measured on round jets
        constexpr float lighthill_constant = 5.0e-5f;
        constexpr float jet_noise_peak_hz  = 4000.0f;

        // valve events in crank degrees after firing tdc, the exhaust valve opening is the event clock
        constexpr float exhaust_valve_open_deg = 135.0f;
        constexpr float intake_valve_open_deg  = 350.0f;

        float sound_speed(float temperature)
        {
            return sqrtf(gas_gamma * gas_constant * temperature);
        }

        // slow moving state such as filter cutoffs is refreshed every this many samples
        constexpr int control_interval = 64;
        constexpr int oversampling = 2;

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
            // the wave that last came back to the entrance, what the valve sees on the other side
            float returned   = 0.0f;
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
                returned = feedback * loss.process(line.read(round_trip));
                float arriving = line.read(round_trip * 0.5f);
                line.write(input + returned);
                return arriving;
            }

            // a pipe that has rung down stops being computed until something is fed into it again
            float process_gated(float input)
            {
                if (input == 0.0f && quiet_samples > line.mask)
                {
                    returned = 0.0f;
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
                returned = 0.0f;
                quiet_samples = 0;
            }
        };

        // one direction of travel through a duct; a pipe carries two of these, one each way, and whatever meets
        // their ends (a valve, a junction, a can) decides what is sent back
        struct duct
        {
            delay_line line;
            float base_delay = 4.0f; // at the design gas temperature
            float delay      = 4.0f;
            float gain       = 1.0f;
            one_pole loss;
            bool lossy = false;

            void configure(float length_m, float sound_speed, float pass_gain, float loss_hz, float sample_rate)
            {
                base_delay = delay = std::max(2.0f, length_m / sound_speed * sample_rate);
                line.resize(static_cast<int>(base_delay) + 8);
                gain  = pass_gain;
                lossy = loss_hz > 0.0f;
                if (lossy)
                {
                    loss.set_cutoff(loss_hz, sample_rate);
                }
            }

            void set_speed_scale(float scale)
            {
                delay = std::max(2.0f, base_delay / std::max(scale, 1.0f));
            }

            // what arrives at the far end now, read before this sample's write
            float arrive()
            {
                const float x = line.read(delay) * gain;
                return lossy ? loss.process(x) : x;
            }

            void send(float x)
            {
                line.write(x);
            }

            void reset()
            {
                line.clear();
                loss.reset();
            }
        };

        // a strong pressure wave does not keep its shape: its crest rides on gas already pushed forward and heated,
        // so it runs faster than its foot and the front steepens, into a shock if the path is long enough; this is
        // what puts the crack in an exhaust pulse; samples keep their value and only their arrival time moves
        struct steepener
        {
            std::vector<float>  value;
            std::vector<double> arrival;
            int mask = 0;
            std::int64_t count = 0;
            std::int64_t read  = 0;
            double last_arrival = 0.0;
            float length_m = 0.0f;
            float sample_rate = 48000.0f;
            // samples a faint wave takes over the path, and how much faster a bar of wave runs relative to that
            float travel = 0.0f;
            float speedup_per_bar = 0.0f;

            void configure(float in_length_m, float slowest_sound_speed, float in_sample_rate)
            {
                length_m = in_length_m;
                sample_rate = in_sample_rate;
                // a rarefaction may run at a third of the speed, so the buffer holds three trips and a margin
                int size = 64;
                while (size < static_cast<int>(3.5f * length_m / slowest_sound_speed * sample_rate) + 16)
                {
                    size <<= 1;
                }
                value.assign(static_cast<size_t>(size), 0.0f);
                arrival.assign(static_cast<size_t>(size), 0.0);
                mask = size - 1;
                reset();
            }

            // the wave speed is c (1 + (gamma + 1) / 2 u / c), and u / c = p / (gamma p_mean) for a simple wave
            void set_gas(float sound_speed_mps, float mean_pressure)
            {
                travel = length_m / sound_speed_mps * sample_rate;
                speedup_per_bar = 0.5f * (gas_gamma + 1.0f) * pascal_per_bar / (gas_gamma * mean_pressure);
            }

            float process(float x)
            {
                // everything is late by half a trip, so a crest running up to twice as fast still arrives in the future
                const float speed = std::clamp(1.0f + speedup_per_bar * x, 0.3f, 2.0f);
                double t = static_cast<double>(count) + travel * (0.5f + 1.0f / speed - 1.0f);
                // a crest cannot pass the foot ahead of it, where it catches up the front stands vertical
                t = std::max(t, last_arrival);
                last_arrival = t;
                value[static_cast<size_t>(count & mask)]   = x;
                arrival[static_cast<size_t>(count & mask)] = t;
                count++;

                const double now = static_cast<double>(count - 1);
                while (read + 1 < count && arrival[static_cast<size_t>((read + 1) & mask)] <= now)
                {
                    read++;
                }
                const double a0 = arrival[static_cast<size_t>(read & mask)];
                if (read + 1 >= count || a0 > now)
                {
                    return 0.0f;
                }
                const double a1 = arrival[static_cast<size_t>((read + 1) & mask)];
                const float v0 = value[static_cast<size_t>(read & mask)];
                const float v1 = value[static_cast<size_t>((read + 1) & mask)];
                const float f  = a1 > a0 ? static_cast<float>((now - a0) / (a1 - a0)) : 1.0f;
                return v0 + (v1 - v0) * f;
            }

            // the half trip every sample is held back by, what a wave too faint to steepen takes through here
            float latency() const
            {
                return 0.5f * travel;
            }

            void reset()
            {
                std::fill(value.begin(), value.end(), 0.0f);
                std::fill(arrival.begin(), arrival.end(), 0.0);
                count = 0;
                read = 0;
                last_arrival = 0.0;
            }
        };

        // a packed, reverse-flow can of three chambers joined by tubes: well below its pipe modes each chamber's
        // air springs against the slug of gas in the tube leaving it, a helmholtz resonator that passes the firing
        // pulses; above that each holds an expansion chamber's loss, the losses add, and the packing soaks up
        // the chambers' own pipe modes
        struct muffler
        {
            static constexpr int chamber_count = 3;
            biquad   helmholtz[chamber_count];
            float    helmholtz_hz[chamber_count] = {}; // at the design gas temperature
            one_pole lining;
            float plateau     = 1.0f;
            float sample_rate = 48000.0f;
            // perforate and flow resistance, what keeps the chambers from booming
            static constexpr float helmholtz_q = 0.5f;

            // a straight pipe at zero, a sports can is mostly a packed straight-through tube with small chambers,
            // a fully silenced road can has chambers six times the pipe's area, some 30 db in the firing band
            static float area_ratio(float level)
            {
                return 1.0f + 5.0f * level * level * level;
            }

            void configure(float level, float displacement_l, float tailpipe_length_m, float tailpipe_radius_m, float temperature, float rate)
            {
                sample_rate = rate;
                const float m = area_ratio(level);
                plateau = 2.0f * m / (m * m + 1.0f);

                // bigger engines carry bigger cans; each tube's air moves as one slug, lengthened at both ends
                const float can_length = std::clamp(0.25f + 0.07f * displacement_l, 0.25f, 0.6f);
                const float end_correction = 1.5f * tailpipe_radius_m;
                const float chamber_lengths[chamber_count] = { 0.5f * can_length, 0.3f * can_length, 0.2f * can_length };
                const float necks[chamber_count] = { 0.3f * can_length + end_correction, 0.3f * can_length + end_correction, tailpipe_length_m + end_correction };
                for (int i = 0; i < chamber_count; i++)
                {
                    helmholtz_hz[i] = sound_speed(temperature) / two_pi * sqrtf(1.0f / (m * chamber_lengths[i] * necks[i]));
                    helmholtz[i].set_lowpass(helmholtz_hz[i], helmholtz_q, sample_rate);
                }

                // fibre packing absorbs from a few hundred hz up, a thin wrap only takes the top off
                lining.set_cutoff(6000.0f * powf(0.07f, level), sample_rate);
            }

            void set_speed_scale(float scale)
            {
                for (int i = 0; i < chamber_count; i++)
                {
                    helmholtz[i].set_lowpass(helmholtz_hz[i] * scale, helmholtz_q, sample_rate);
                }
            }

            float process(float x)
            {
                for (int i = 0; i < chamber_count; i++)
                {
                    x = plateau * x + (1.0f - plateau) * helmholtz[i].process(x);
                }
                return lining.process(x);
            }

            void reset()
            {
                for (int i = 0; i < chamber_count; i++)
                {
                    helmholtz[i].reset();
                }
                lining.reset();
            }
        };

        struct cylinder
        {
            int   bank        = 0;
            float fire_angle  = 0.0f;
            float imbalance   = 1.0f;
            // crank degrees since the exhaust valve opened, huge means the valve is shut
            float valve_phase = 1.0e9f;
            // the trapped charge while the exhaust valve is open, si units
            float gas_mass     = 0.0f;
            float gas_pressure = ambient_pressure;
            float gas_volume   = 0.0f;
            // how much of the valve jet's turbulent roar is heard, one is lighthill's estimate
            float turbulence   = 0.0f;
            // the primary pipe, down toward the collector and back up toward the valve; the first half of the way
            // down is the steepener for a fresh pulse and a plain delay for the wave bouncing off the valve face
            steepener  front;
            delay_line echo;
            duct  down;
            duct  up;
            // what reached the valve and the junction this sample
            float at_valve    = 0.0f;
            float at_junction = 0.0f;
        };

        struct exhaust_bank
        {
            // the collector, down from the junction toward the can and back up
            steepener  collector_front;
            duct       collector_down;
            duct       collector_up;
            steepener  tailpipe_front;
            // a turbine wheel takes energy out of each pulse and smears it
            one_pole   turbine;
            muffler    can;
            pipe       tailpipe;
            dc_blocker dc;
            // what the tailpipe radiates, the open end differentiates below its cutoff
            one_pole   radiation;
            one_pole   exit_reflection;
            // afterfire and overrun pops are injected here, at the collector; a pocket of charge burns in a fraction
            // of a millisecond and vents over several, the pressure is the slow decay less the fast one
            float pop_env        = 0.0f;
            float pop_rise       = 0.0f;
            float pop_decay      = 0.99f;
            float pop_rise_decay = 0.9f;
            // pockets the flame front reaches later, samples to go and their share of the first
            float pop_kick_delay[2] = { -1.0f, -1.0f };
            float pop_kick_amp[2]   = { 0.0f, 0.0f };
            float cylinder_count = 1.0f;

            void reset()
            {
                collector_front.reset();
                tailpipe_front.reset();
                collector_down.reset();
                collector_up.reset();
                turbine.reset();
                can.reset();
                tailpipe.reset();
                dc.reset();
                radiation.reset();
                exit_reflection.reset();
                pop_env  = 0.0f;
                pop_rise = 0.0f;
                pop_kick_delay[0] = pop_kick_delay[1] = -1.0f;
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
            float openness        = 0.1f;
            bool  odd_fire        = false;
            // gas torque of the last sample, drives the crank speed ripple
            float torque_ripple   = 0.0f;
            float torque_mean     = 0.0f;

            // cylinder volume over the whole cycle, clearance plus what the piston has swept
            static constexpr int volume_steps_per_degree = 2;
            std::vector<float> volume_table;
            // effective flow areas of the exhaust valves, the curtain at full lift and the throat that caps it
            float valve_curtain  = 0.0f;
            float valve_throat   = 0.0f;
            float valve_duration = 240.0f;
            // cylinder pressure at exhaust valve opening over manifold pressure, for a burning charge
            float fired_pressure_ratio = 4.6f;
            // choked flow of a full charge through the open valves, only used to normalise envelopes
            float reference_flow = 1.0f;

            // pipe acoustics, waves are carried in bar
            float primary_area      = 1.0e-3f;
            float primary_impedance = 1.0f; // bar of wave per kg/s of flow, follows the gas temperature
            float collector_area    = 1.0e-3f;
            // what the can sends back up the collector, an area step out is an inverted reflection
            float can_reflection    = -0.8f;
            float tailpipe_area     = 1.0e-3f;
            float tailpipe_feedback = 0.0f; // without flow
            // where the exhaust stream separates into a jet it sheds vortices that eat sound, a reflection keeps
            // (1 - m) / (1 + m) of itself at mean flow mach m; negligible at idle, large flat out
            float primary_flow_damping   = 1.0f;
            float collector_flow_damping = 1.0f;
            float tailpipe_radius   = 0.03f;
            float radiation_gain    = 0.0f;
            float turbine_transmission = 1.0f;
            float gas_temperature   = primary_cold_temperature;
            float back_pressure     = ambient_pressure;

            float volume(float crank_deg) const
            {
                float f = fmodf(std::max(crank_deg, 0.0f), 720.0f) * volume_steps_per_degree;
                int i = std::min(static_cast<int>(f), static_cast<int>(volume_table.size()) - 2);
                return lerp(volume_table[i], volume_table[i + 1], f - static_cast<float>(i));
            }

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

                openness = std::clamp(1.1f - config.muffler_level, 0.1f, 1.0f);

                // slider crank with a typical rod of 1.6 strokes
                const float bore         = std::max(config.bore_mm, 20.0f) * 1.0e-3f;
                const float stroke       = std::max(config.stroke_mm, 20.0f) * 1.0e-3f;
                const float crank_radius = 0.5f * stroke;
                const float rod_length   = 1.6f * stroke;
                const float piston_area  = 0.25f * pi * bore * bore;
                const float clearance    = piston_area * stroke / std::max(config.compression_ratio - 1.0f, 1.0f);
                volume_table.resize(720 * volume_steps_per_degree + 2);
                for (size_t i = 0; i < volume_table.size(); i++)
                {
                    float theta = static_cast<float>(i) / volume_steps_per_degree * pi / 180.0f;
                    float s = sinf(theta);
                    float travel = crank_radius * (1.0f - cosf(theta)) + rod_length - sqrtf(rod_length * rod_length - crank_radius * crank_radius * s * s);
                    volume_table[i] = clearance + piston_area * travel;
                }

                // heads are sized to breathe at the redline: taylor's inlet mach index, (bore / valves)^2 * piston speed
                // / (mean flow coefficient * sound speed), is held near 0.5; two exhaust valves of 0.85 the intake's
                // diameter lift a quarter of theirs, discharge coefficient 0.65
                const float piston_speed   = 2.0f * config.stroke_mm * 1.0e-3f * std::max(config.redline_rpm, 3000.0f) / 60.0f;
                const float intake_valves  = bore * sqrtf(piston_speed / (0.5f * 0.35f * 343.0f));
                const float valve_diameter = std::clamp(0.85f * intake_valves / sqrtf(2.0f), 0.28f * bore, 0.4f * bore);
                const float valve_ports    = 2.0f * 0.25f * pi * valve_diameter * valve_diameter;
                valve_curtain  = 0.65f * 2.0f * pi * valve_diameter * 0.26f * valve_diameter;
                valve_throat   = 0.65f * 0.9f * valve_ports;
                valve_duration = std::clamp(config.intake_valve_duration_deg + 20.0f + 20.0f * config.engine_stage, 200.0f, 320.0f);
                // a higher expansion ratio takes more out of the gas before the valve opens
                fired_pressure_ratio = 4.6f * powf(10.0f / std::max(config.compression_ratio, 6.0f), 0.25f);
                const float psi_choked = sqrtf(gas_gamma) * powf(2.0f / (gas_gamma + 1.0f), (gas_gamma + 1.0f) / (2.0f * (gas_gamma - 1.0f)));
                reference_flow = valve_throat * 5.0e5f / sqrtf(gas_constant * 1200.0f) * psi_choked;

                // each bank's primaries meet in a collector, a muffler can sits on it, a tailpipe leaves it
                const float per_bank       = ceilf(static_cast<float>(n) / static_cast<float>(nb));
                primary_area               = 1.25f * valve_ports;
                collector_area             = primary_area * (0.5f + 0.45f * per_bank);
                tailpipe_area              = 0.25f * pi * 0.06f * 0.06f * powf(std::max(config.displacement_l, 0.3f) / 2.0f, 0.8f) / static_cast<float>(nb);
                tailpipe_radius            = sqrtf(tailpipe_area / pi);
                // area steps reflect: the collector sees the can, the tailpipe sees the can behind it and the open end ahead
                const float chamber_ratio      = muffler::area_ratio(config.muffler_level);
                const float into_can           = (1.0f - chamber_ratio) / (1.0f + chamber_ratio);
                can_reflection                 = into_can;
                tailpipe_feedback              = 0.95f * -into_can;
                // walls take a little of every pass, bends and welds a little more
                const float pass_gain          = 0.95f;
                // an open end reflects low frequencies and lets the rest through, the corner is where ka is one
                auto open_end_hz = [](float area, float temperature)
                {
                    return sound_speed(temperature) / (two_pi * sqrtf(area / pi));
                };
                const float primary_loss_hz   = open_end_hz(primary_area, primary_cold_temperature);
                const float collector_loss_hz = open_end_hz(collector_area, collector_cold_temperature);
                const float tailpipe_loss_hz  = open_end_hz(tailpipe_area, tailpipe_cold_temperature);
                // a recording sets its gain to the car: a quiet road can is recorded hotter, making up half of what
                // its chambers take in the firing band, so cars stay in their order of loudness but all stay heard
                const float chamber_pass    = 2.0f * chamber_ratio / (chamber_ratio * chamber_ratio + 1.0f);
                const float recording_gain  = powf(chamber_pass, -0.5f * static_cast<float>(muffler::chamber_count));
                radiation_gain       = tailpipe_radius / (4.0f * listener_distance_m) * pascal_per_bar / full_scale_pascal * recording_gain;
                turbine_transmission = config.turbo_enabled ? 0.5f : 1.0f;

                rng seed_rng;
                seed_rng.seed(0xC0FFEE00u + static_cast<std::uint32_t>(n * 131 + nb));

                // where each cylinder sits along its bank, the header merges at the middle of the bank
                std::vector<int> bank_position(static_cast<size_t>(n), 0);
                std::vector<int> bank_size(static_cast<size_t>(nb), 0);
                for (int i = 0; i < n; i++)
                {
                    const int bank = std::clamp(config.cylinder_bank[i], 0, nb - 1);
                    bank_position[static_cast<size_t>(i)] = bank_size[static_cast<size_t>(bank)]++;
                }
                const float cylinder_pitch = 1.12f * bore;

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
                    // cylinders breathe slightly differently, most of the spread is cycle to cycle
                    c.imbalance = 1.0f + 0.4f * config.combustion_variation * seed_rng.bipolar();

                    // an outer cylinder is further from the merge than an inner one, the routing of the pipes only
                    // makes up part of it; the rest is a bend here and a weld there
                    const float centre       = 0.5f * static_cast<float>(bank_size[static_cast<size_t>(bank)] - 1);
                    const float from_centre  = fabsf(static_cast<float>(bank_position[static_cast<size_t>(cylinder_index)]) - centre);
                    const float mean_from_centre = 0.25f * static_cast<float>(bank_size[static_cast<size_t>(bank)]);
                    const float unequal      = (1.0f - header_equalization) * (from_centre - mean_from_centre) * cylinder_pitch;
                    float length = std::max(config.primary_length_m + unequal, 0.1f) * (1.0f + 0.01f * seed_rng.bipolar());
                    c.down.configure(0.5f * length, sound_speed(primary_cold_temperature), pass_gain, 0.0f, sample_rate);
                    c.echo.resize(static_cast<int>(0.5f * length / sound_speed(primary_cold_temperature) * sample_rate) + 8);
                    c.up.configure(length, sound_speed(primary_cold_temperature), pass_gain, primary_loss_hz, sample_rate);
                    c.front.configure(length, sound_speed(primary_cold_temperature), sample_rate);
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
                    // one bank's pipe crosses over to reach the rear, the two runs are never the same length
                    const float collector_length = config.collector_length_m * (1.0f + (nb > 1 ? (b == 0 ? -0.5f : 0.5f) * bank_run_mismatch : 0.0f));
                    bank.collector_down.configure(collector_length, sound_speed(collector_cold_temperature), pass_gain, 0.0f, sample_rate);
                    bank.collector_up.configure(collector_length, sound_speed(collector_cold_temperature), pass_gain, collector_loss_hz, sample_rate);
                    // pulses only steepen as far as the catalytic converter, its fine channels break the fronts up
                    bank.collector_front.configure(std::min(config.collector_length_m, catalyst_distance_m), sound_speed(collector_cold_temperature), sample_rate);
                    bank.tailpipe_front.configure(config.tailpipe_length_m, sound_speed(tailpipe_cold_temperature), sample_rate);
                    bank.turbine.set_cutoff(2500.0f, sample_rate);
                    bank.can.configure(config.muffler_level, config.displacement_l, config.tailpipe_length_m, tailpipe_radius, tailpipe_cold_temperature, sample_rate);
                    bank.tailpipe.configure(config.tailpipe_length_m, sound_speed(tailpipe_cold_temperature), tailpipe_feedback, tailpipe_loss_hz, sample_rate);
                    // Keep the bass cutoff in Hz when the internal sample rate changes.
                    bank.dc.r = expf(-two_pi * 18.0f / sample_rate);
                    bank.pop_decay      = expf(-1.0f / ((0.005f + 0.004f * openness) * sample_rate));
                    bank.pop_rise_decay = expf(-1.0f / (0.0004f * sample_rate));
                    bank.cylinder_count = 0.0f;
                    for (const cylinder& c : cylinders)
                    {
                        bank.cylinder_count += c.bank == b ? 1.0f : 0.0f;
                    }
                }

                // each power stroke is a half sine of gas torque over the 180 degrees after tdc
                torque_mean = static_cast<float>(n) * (360.0f / pi) / 720.0f;
                torque_ripple = 0.0f;
                next_event  = 0;
                crank_angle = 0.0f;
                set_gas_temperature(primary_cold_temperature);
            }

            void reset()
            {
                intake_runner.reset();
                for (cylinder& c : cylinders)
                {
                    c.valve_phase = 1.0e9f;
                    c.front.reset();
                    c.echo.clear();
                    c.down.reset();
                    c.up.reset();
                    c.at_valve = c.at_junction = 0.0f;
                }
                for (exhaust_bank& b : banks)
                {
                    b.reset();
                }
                torque_ripple = 0.0f;
                next_event  = 0;
                crank_angle = 0.0f;
            }

            // the intake breathes ambient air, only the exhaust side heats up; the whole system scales with
            // the gas at the head, each pipe keeping its cooler share
            void set_gas_temperature(float primary_temperature)
            {
                gas_temperature = std::max(primary_temperature, primary_cold_temperature);
                const float speed_scale = sqrtf(gas_temperature / primary_cold_temperature);
                primary_impedance = sound_speed(gas_temperature) / primary_area / pascal_per_bar;
                const float collector_temperature = gas_temperature * (collector_cold_temperature / primary_cold_temperature);
                const float tailpipe_temperature = gas_temperature * (tailpipe_cold_temperature / primary_cold_temperature);
                const float radiation_hz = sound_speed(tailpipe_temperature) / (two_pi * tailpipe_radius);
                for (cylinder& c : cylinders)
                {
                    c.down.set_speed_scale(speed_scale);
                    c.up.set_speed_scale(speed_scale);
                    c.front.set_gas(sound_speed(gas_temperature), back_pressure);
                }
                for (exhaust_bank& b : banks)
                {
                    b.collector_front.set_gas(sound_speed(collector_temperature), back_pressure);
                    b.tailpipe_front.set_gas(sound_speed(tailpipe_temperature), ambient_pressure);
                    b.collector_down.set_speed_scale(speed_scale);
                    b.collector_up.set_speed_scale(speed_scale);
                    b.can.set_speed_scale(speed_scale);
                    b.tailpipe.set_speed_scale(speed_scale);
                    b.radiation.set_cutoff(radiation_hz, sample_rate);
                    b.exit_reflection.set_cutoff(radiation_hz, sample_rate);
                }
            }
        };

        // mean exhaust mass flow of one cylinder, kg/s, sets the stream's mach number in each pipe
        inline void set_mean_flow(engine_model& model, float cylinder_flow)
        {
            auto damping = [&](float flow, float area, float temperature)
            {
                const float density = model.back_pressure / (gas_constant * temperature);
                const float mach = std::clamp(flow / (density * area * sound_speed(temperature)), 0.0f, 0.6f);
                return (1.0f - mach) / (1.0f + mach);
            };
            const float collector_temperature = model.gas_temperature * (collector_cold_temperature / primary_cold_temperature);
            const float tailpipe_temperature  = model.gas_temperature * (tailpipe_cold_temperature / primary_cold_temperature);
            model.primary_flow_damping = damping(cylinder_flow, model.primary_area, model.gas_temperature);
            for (exhaust_bank& b : model.banks)
            {
                const float bank_flow = cylinder_flow * b.cylinder_count;
                model.collector_flow_damping = damping(bank_flow, model.collector_area, collector_temperature);
                b.tailpipe.feedback = model.tailpipe_feedback * damping(bank_flow, model.tailpipe_area, tailpipe_temperature);
            }
        }

        // per sample shapes, tabulated once instead of powf and sinf per cylinder at 96 khz
        struct shape_tables
        {
            static constexpr int half_sine_steps = 1024;
            static constexpr int flow_steps = 2048;
            float half_sine[half_sine_steps + 1] = {};
            // isentropic nozzle flow against the downstream over upstream pressure ratio, choked below the critical ratio
            float mass_flux[flow_steps + 1] = {};
            float mach_squared[flow_steps + 1] = {};

            shape_tables()
            {
                for (int i = 0; i <= half_sine_steps; i++)
                {
                    half_sine[i] = sinf(pi * static_cast<float>(i) / static_cast<float>(half_sine_steps));
                }
                const float g = gas_gamma;
                const float critical = powf(2.0f / (g + 1.0f), g / (g - 1.0f));
                for (int i = 0; i <= flow_steps; i++)
                {
                    float ratio = std::max(static_cast<float>(i) / static_cast<float>(flow_steps), critical);
                    mass_flux[i] = sqrtf(std::max(2.0f * g / (g - 1.0f) * (powf(ratio, 2.0f / g) - powf(ratio, (g + 1.0f) / g)), 0.0f));
                    mach_squared[i] = std::min(2.0f / (g - 1.0f) * (powf(ratio, -(g - 1.0f) / g) - 1.0f), 1.0f);
                }
            }

            float lookup(const float* table, float ratio) const
            {
                float f = std::clamp(ratio, 0.0f, 1.0f) * flow_steps;
                int i = std::min(static_cast<int>(f), flow_steps - 1);
                return lerp(table[i], table[i + 1], f - static_cast<float>(i));
            }

            // mass flow per unit area per upstream pressure over sqrt(r t)
            float flux(float ratio) const
            {
                return lookup(mass_flux, ratio);
            }

            float mach2(float ratio) const
            {
                return lookup(mach_squared, ratio);
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
            float master_gain = 1.6f; // drives the output limiter, peaks stay under 0.85 while the body gets louder
        };

        // a dump carries the controls it was made with, one row per 10 ms at 48 khz, so it can be order analysed
        constexpr int dump_row_interval = 480;

        struct dump_row
        {
            float time = 0.0f;
            float rpm = 0.0f;
            float rpm_heard = 0.0f;
            float throttle = 0.0f;
            float load = 0.0f;
            float boost = 0.0f;
            bool fuel_cut = false;
            bool overrun = false;
            bool shifting = false;
            int gear = 0;
            float limiter_gain = 1.0f;
            int pops_fired = 0;
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
            // a valve jet's roar peaks near a strouhal number of 0.2, about 4 khz for a sonic jet through a port;
            // scaled to unit variance, the band passes pi f / (q fs) of uniform noise's power of a third
            m_jet_noise.set_bandpass(jet_noise_peak_hz, 0.5f, m_sample_rate);
            m_jet_noise_scale = 1.0f / sqrtf(pi * jet_noise_peak_hz / (0.5f * m_sample_rate) / 3.0f);
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
            // both ears hear both tailpipes, the far pipe only arrives a little later
            {
                const float pipe_spacing_m = 1.0f;
                const float ear_spacing_m  = 0.18f;
                const float near_path = hypotf(listener_distance_m, 0.5f * (pipe_spacing_m - ear_spacing_m));
                const float far_path  = hypotf(listener_distance_m, 0.5f * (pipe_spacing_m + ear_spacing_m));
                const float air_sound_speed = 343.0f;
                m_ear_lag_samples = (far_path - near_path) / air_sound_speed * m_sample_rate;
                for (delay_line& line : m_bank_delay)
                {
                    line.resize(static_cast<int>(ceilf(m_ear_lag_samples)) + 4);
                }
            }

            m_limiter_release = expf(-1.0f / (0.05f * m_sample_rate));
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
                const float combustion_noise = m_jet_noise.process(white) * m_jet_noise_scale;

                model_frame frame;
                step_model(model, p, rpm, load, rpm_norm, boost_norm, fuel_cut, combustion_noise, dt, frame);
                if (m_fading && m_crossfade < 1.0f)
                {
                    // both engines play for a moment, equal power, so an upgrade never drops out
                    // the new pipes start empty, so they run unheard until their first pulses are through
                    model_frame previous;
                    step_model(*m_fading, p, rpm, load, rpm_norm, boost_norm, fuel_cut, combustion_noise, dt, previous);
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

                // mix, body, limiter; each bank's pipe sits on its side of the car, so it reaches the ear on that
                // side first and the other ear a fraction of a millisecond later, less so the more side on we stand
                float center = intake + turbo + mech;
                m_bank_delay[0].write(bank_out[0]);
                m_bank_delay[1].write(bank_out[1]);
                const float lag = 1.0f + m_ear_lag_samples * fabsf(m_bank_pan);
                const bool bank_0_left = m_bank_pan >= 0.0f;
                float left   = 0.5f * (m_bank_delay[0].read(bank_0_left ? 1.0f : lag) + m_bank_delay[1].read(bank_0_left ? lag : 1.0f));
                float right  = 0.5f * (m_bank_delay[1].read(bank_0_left ? 1.0f : lag) + m_bank_delay[0].read(bank_0_left ? lag : 1.0f));
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
                    if (m_dump_progress % dump_row_interval == 0 && m_dump_rows.size() < m_dump_rows.capacity())
                    {
                        dump_row row;
                        row.time = static_cast<float>(m_dump_progress) / m_output_sample_rate;
                        row.rpm = target_rpm;
                        row.rpm_heard = m_rpm_smooth.z;
                        row.throttle = target_throttle;
                        row.load = target_load;
                        row.boost = target_boost;
                        row.fuel_cut = fuel_cut;
                        row.overrun = overrun;
                        row.shifting = shifting;
                        row.gear = gear;
                        row.limiter_gain = limiter_gain;
                        row.pops_fired = m_debug.pops_fired;
                        m_dump_rows.push_back(row);
                    }
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

        int get_output_sample_rate() const
        {
            return static_cast<int>(m_output_sample_rate);
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
            // reserved up front, the audio thread only ever appends within capacity
            m_dump_rows.clear();
            m_dump_rows.reserve(static_cast<size_t>(m_dump_total / dump_row_interval + 2));
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
            if (result)
            {
                // the control track sits next to the audio, same name with a csv extension
                std::string csv_path = path;
                size_t dot = csv_path.find_last_of('.');
                size_t slash = csv_path.find_last_of("/\\");
                if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
                {
                    csv_path.resize(dot);
                }
                csv_path += ".csv";
                FILE* file = nullptr;
                fopen_s(&file, csv_path.c_str(), "wb");
                if (file)
                {
                    fprintf(file, "time,rpm,rpm_heard,throttle,load,boost,fuel_cut,overrun,shifting,gear,limiter_gain,pops_fired\n");
                    for (const dump_row& row : m_dump_rows)
                    {
                        fprintf(file, "%.4f,%.1f,%.1f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%.4f,%d\n", row.time, row.rpm, row.rpm_heard, row.throttle, row.load, row.boost,
                            row.fuel_cut ? 1 : 0, row.overrun ? 1 : 0, row.shifting ? 1 : 0, row.gear, row.limiter_gain, row.pops_fired);
                    }
                    fclose(file);
                }
            }
            m_dump_rows.clear();
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

        // one sample of blowdown through the exhaust valves; the port pressure is what the pipe pushes back
        // with plus the wave this very flow launches, so the flow is found where the two agree
        float valve_flow(const engine_model& model, cylinder& c, const shape_tables& tables, float dt, float& jet_noise)
        {
            // a cam's ramps are short, the valve is well off its seat within a few degrees and lifts like a sine
            const float lift   = tables.sine_lobe(c.valve_phase / model.valve_duration);
            // the gap under the valve head is a ring, its area grows with lift until the port throat caps it
            const float area   = std::min(model.valve_curtain * lift, model.valve_throat);
            const float volume = model.volume(exhaust_valve_open_deg + c.valve_phase);
            const float p_cyl  = c.gas_pressure;
            const float t_cyl  = std::max(p_cyl * c.gas_volume / (c.gas_mass * gas_constant), 250.0f);
            // the valve face is a wall with a hole in it, the returning wave doubles against it
            const float p_ext  = std::clamp(model.back_pressure + 2.0f * c.at_valve * pascal_per_bar, 0.3f * ambient_pressure, 5.0f * ambient_pressure);
            const float impedance = model.primary_impedance * pascal_per_bar;
            const float out_scale = area * p_cyl / sqrtf(gas_constant * t_cyl);
            const float in_scale  = area / sqrtf(gas_constant * model.gas_temperature);

            auto flow_at = [&](float p_port)
            {
                return p_cyl >= p_port ? out_scale * tables.flux(p_port / p_cyl) : -in_scale * p_port * tables.flux(p_cyl / p_port);
            };

            // p - p_ext - z * flow(p) rises with p and changes sign between the two pressures, bisection cannot diverge;
            // the last bracket is interpolated, a bracket left coarse would hiss like a noise floor
            auto mismatch = [&](float p_port)
            {
                return p_port - p_ext - impedance * flow_at(p_port);
            };
            float lo = std::min(p_cyl, p_ext);
            float hi = std::max(p_cyl, p_ext);
            for (int i = 0; i < 14; i++)
            {
                float mid = 0.5f * (lo + hi);
                if (mismatch(mid) > 0.0f)
                {
                    hi = mid;
                }
                else
                {
                    lo = mid;
                }
            }
            const float g_lo   = mismatch(lo);
            const float g_hi   = mismatch(hi);
            const float p_port = g_hi > g_lo ? lo - g_lo * (hi - lo) / (g_hi - g_lo) : 0.5f * (lo + hi);
            const float flow   = flow_at(p_port);

            // lighthill: a jet radiates k rho u^8 d^2 / c^5 of sound into the gas around it, here the pipe's
            const bool  outward   = p_cyl >= p_port;
            const float mach2     = outward ? tables.mach2(p_port / p_cyl) : tables.mach2(p_cyl / p_port);
            const float t_source  = outward ? t_cyl : model.gas_temperature;
            const float jet_speed = sqrtf(mach2 * gas_gamma * gas_constant * t_source / (1.0f + 0.5f * (gas_gamma - 1.0f) * mach2));
            const float pipe_density = p_ext / (gas_constant * model.gas_temperature);
            const float pipe_sound   = sound_speed(model.gas_temperature);
            const float u2 = jet_speed * jet_speed / (pipe_sound * pipe_sound);
            const float power = lighthill_constant * pipe_density * pipe_sound * pipe_sound * pipe_sound * u2 * u2 * u2 * u2 * (4.0f / pi) * area;
            jet_noise = sqrtf(power * pipe_density * pipe_sound / model.primary_area) / pascal_per_bar;

            // open system, adiabatic: what leaves carries the cylinder's temperature, what returns the pipe's
            const float dm = -flow * dt;
            const float t_flow = flow > 0.0f ? t_cyl : model.gas_temperature;
            const float dp = (gas_gamma * gas_constant * t_flow * dm - gas_gamma * p_cyl * (volume - c.gas_volume)) / volume;
            c.gas_mass     = std::max(c.gas_mass + dm, 1.0e-8f);
            c.gas_pressure = std::max(p_cyl + dp, 0.1f * ambient_pressure);
            c.gas_volume   = volume;
            return flow;
        }

        void step_model(engine_model& model, const mix_levels& p, float rpm, float load, float rpm_norm, float boost_norm, bool fuel_cut, float combustion_noise, float dt, model_frame& frame)
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

            // cylinders into their primaries, a cylinder's event angle is its exhaust valve opening
            const float duration = std::clamp(cfg.intake_valve_duration_deg, 120.0f, 320.0f);
            const float inverse_duration = 1.0f / duration;
            const float inverse_reference_flow = 1.0f / model.reference_flow;
            float bank_in[2] = { 0.0f, 0.0f };
            float induction = 0.0f;
            float torque = 0.0f;
            for (cylinder& c : model.cylinders)
            {
                c.at_valve    = c.up.arrive();
                c.at_junction = c.down.arrive();
                float excitation = 0.0f;
                if (c.valve_phase < model.valve_duration)
                {
                    float jet_noise = 0.0f;
                    const float flow = valve_flow(model, c, tables, dt, jet_noise);
                    // the wave the flow launches, plus the roar of the jet's turbulence
                    excitation = model.primary_impedance * flow + c.turbulence * jet_noise * combustion_noise;
                    frame.pulse_env += std::max(flow, 0.0f) * inverse_reference_flow;
                    c.valve_phase += deg_per_sample;
                }
                // the valve face sends back what arrives, plus what the flow launches
                const float echo = c.echo.read(c.front.latency());
                c.echo.write(c.at_valve);
                c.down.send(echo + c.front.process(excitation));
                bank_in[c.bank] += c.at_junction;

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

            // each junction settles at one pressure, set by every pipe that meets it weighted by its area; each
            // pipe then carries away that pressure less what it brought, so a pulse runs on down the collector and
            // also up the sibling primaries, off their shut valves and back
            const int nb = static_cast<int>(model.banks.size());
            float junction[2] = { 0.0f, 0.0f };
            float collector_back[2] = { 0.0f, 0.0f };
            for (int b = 0; b < nb; b++)
            {
                exhaust_bank& bank = model.banks[static_cast<size_t>(b)];
                collector_back[b] = bank.collector_up.arrive();
                const float total_area = bank.cylinder_count * model.primary_area + model.collector_area;
                junction[b] = 2.0f * (model.primary_area * bank_in[b] + model.collector_area * collector_back[b]) / total_area;
            }
            for (cylinder& c : model.cylinders)
            {
                c.up.send((junction[c.bank] - c.at_junction) * model.primary_flow_damping);
            }

            // per bank collector, muffler, tailpipe
            for (int b = 0; b < nb; b++)
            {
                exhaust_bank& bank = model.banks[static_cast<size_t>(b)];
                for (int k = 0; k < 2; k++)
                {
                    if (bank.pop_kick_delay[k] >= 0.0f)
                    {
                        bank.pop_kick_delay[k] -= 1.0f;
                        if (bank.pop_kick_delay[k] < 0.0f)
                        {
                            bank.pop_env  += bank.pop_kick_amp[k];
                            bank.pop_rise += bank.pop_kick_amp[k];
                        }
                    }
                }
                float pop = 0.0f;
                if (bank.pop_env > 1.0e-4f)
                {
                    // fuel lighting off in the collector, a few tenths of a bar
                    pop = 0.25f * (bank.pop_env - bank.pop_rise);
                    bank.pop_env  *= bank.pop_decay;
                    bank.pop_rise *= bank.pop_rise_decay;
                }
                else
                {
                    bank.pop_env  = 0.0f;
                    bank.pop_rise = 0.0f;
                }
                frame.pops += pop;

                float x = bank.collector_down.arrive();
                bank.collector_down.send(junction[b] - collector_back[b] + pop);
                bank.collector_up.send(model.can_reflection * model.collector_flow_damping * x);
                // what goes on into the can has steepened on its way down, outside the loop so no bounce steepens twice
                x = bank.collector_front.process(x);
                if (cfg.turbo_enabled)
                {
                    x = bank.turbine.process(x) * model.turbine_transmission;
                }
                x = bank.can.process(x);
                x = bank.tailpipe.process(bank.tailpipe_front.process(x));
                x = bank.dc.process(x);
                // the open end throws the bass back up the pipe inverted, so the gas at the exit moves with the
                // arriving wave plus its reflection, twice the wave at low frequencies and just the wave above ka = 1
                x += bank.exit_reflection.process(x);
                // far field of the open end, rising with frequency below its corner and flat above
                x = (x - bank.radiation.process(x)) * model.radiation_gain;
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
            m_bank_delay[0].clear();
            m_bank_delay[1].clear();
            m_decimator.reset();
            m_jet_noise.reset();
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
            const engine_config& cfg = model.config;
            c.valve_phase = std::max(lead_deg, 0.0f);

            // the limiter drops three sparks in four on a fixed rotation; closed throttle overrun cuts fuel to all
            // cylinders at once, the film left on the ports leans every charge out together over a few cycles
            const bool cut = fuel_cut && (m_cut_counter++ % 4u) != 3u;
            const float fuel = 1.0f - m_overrun_level;
            bool burning = !cut && fuel > 0.02f;
            if (m_start_combustion < 1.0f)
            {
                // early cycles pump air, cylinders begin catching one by one
                burning = burning && m_start_combustion > 0.0f && m_event_rng.uniform() < m_start_combustion;
            }

            // the throttle sets manifold pressure, a turbo adds its boost on top
            const float boost_bar = boost_norm * cfg.boost_max_pressure;
            const float manifold  = ambient_pressure * lerp(0.3f, 1.0f, clamp01(load / 0.85f)) + boost_bar * pascal_per_bar;

            // an unburnt charge is compressed and expanded again, a little is lost to the walls
            float pressure    = 0.9f * manifold;
            float temperature = 450.0f;
            if (burning)
            {
                // cycle to cycle spread, lumpy at idle and light load, steady at full throttle; unit variance
                const float spread = cfg.combustion_variation * (1.0f + 3.0f * (1.0f - clamp01(load)));
                const float cycle  = 1.0f + spread * 1.22f * (m_event_rng.bipolar() + m_event_rng.bipolar());
                // at speed there is less time to lose heat and the burn ends later, more pressure is left
                const float fired = manifold * model.fired_pressure_ratio * (1.0f + 0.15f * rpm_norm) * c.imbalance * std::max(cycle, 0.3f);
                pressure    = lerp(pressure, fired, fuel);
                temperature = lerp(temperature, 1000.0f + 250.0f * clamp01(load), fuel);
                if (m_spark_retard)
                {
                    // a late burn does little work on the piston, the valve opens on hotter, denser gas that is
                    // still burning, and some of it lights off in the pipe
                    pressure    *= 1.3f;
                    temperature += 250.0f;
                    if (m_event_rng.uniform() < 0.15f * clamp01(p.pop_rate))
                    {
                        trigger_pop(model, c.bank, 0.4f + 0.4f * m_event_rng.uniform());
                    }
                }
            }
            c.gas_volume   = model.volume(exhaust_valve_open_deg + c.valve_phase);
            c.gas_pressure = pressure;
            c.gas_mass     = pressure * c.gas_volume / (gas_constant * temperature);
            c.turbulence   = p.rasp;

            // raw charge meeting a hot pipe lights off behind the head
            if (m_start_combustion >= 1.0f && cut && m_event_rng.uniform() < 0.12f * clamp01(p.pop_rate))
            {
                trigger_pop(model, c.bank, 0.6f + 0.6f * m_event_rng.uniform());
            }
        }

        void trigger_pop(engine_model& model, int bank, float amplitude)
        {
            exhaust_bank& b = model.banks[static_cast<size_t>(std::clamp(bank, 0, static_cast<int>(model.banks.size()) - 1))];
            const float a = amplitude * (0.5f + 0.5f * model.openness);
            b.pop_env  += a;
            b.pop_rise += a;
            for (int k = 0; k < 2; k++)
            {
                if (b.pop_kick_delay[k] < 0.0f && m_event_rng.uniform() < 0.5f)
                {
                    b.pop_kick_delay[k] = (0.0015f + 0.0045f * m_event_rng.uniform()) * m_sample_rate;
                    b.pop_kick_amp[k]   = a * (0.25f + 0.35f * m_event_rng.uniform());
                }
            }
            m_debug.pops_fired++;
        }

        void update_control(engine_model& model, const mix_levels& p, float rpm, float rpm_norm, float throttle, float load, float boost_norm, bool fuel_cut, bool overrun, bool shifting, float bank_pan, const float* view_target, float body_cutoff_target, float cabin_gain_target)
        {
            const engine_config& cfg = model.config;
            const float block_dt = static_cast<float>(control_interval) / m_sample_rate;

            m_overrun_level = m_overrun_smooth.process(overrun ? 1.0f : 0.0f);
            // a gearbox cuts torque for the shift by retarding the spark, the throttle stays open
            m_spark_retard = shifting && throttle > 0.5f;
            m_bank_pan = m_bank_pan_smooth.process(bank_pan);

            // exhaust gas temperature at the head, about 600 k at idle and 1150 k flat out, it follows slowly as
            // the pipes heat up; hot gas raises every exhaust resonance
            float gas_heat = m_gas_temperature.process(clamp01(load) * (0.3f + 0.7f * rpm_norm));
            model.set_gas_temperature(600.0f + 550.0f * gas_heat);
            // the flow pushes against the muffler's restriction, and a turbine holds about boost pressure upstream
            const float boost_bar = boost_norm * cfg.boost_max_pressure;
            model.back_pressure = ambient_pressure * (1.0f + 0.2f * cfg.muffler_level * clamp01(load) * (0.3f + 0.7f * rpm_norm));
            if (cfg.turbo_enabled)
            {
                model.back_pressure += 0.8f * boost_bar * pascal_per_bar;
            }
            // each cylinder breathes a charge at manifold density every other turn, and pushes it out as exhaust
            const float manifold = ambient_pressure * lerp(0.3f, 1.0f, clamp01(load / 0.85f)) + boost_bar * pascal_per_bar;
            const float charge = manifold / (gas_constant * 320.0f) * 0.9f * cfg.displacement_l * 1.0e-3f / static_cast<float>(model.cylinders.size());
            set_mean_flow(model, charge * rpm / 120.0f);

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
        std::uint32_t m_cut_counter = 0;
        bool     m_spark_retard  = false;
        float    m_bank_pan      = 1.0f;
        one_pole m_shaft_smooth;
        one_pole m_pulse_env_smooth;
        biquad   m_jet_noise;
        float    m_jet_noise_scale = 1.0f;
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
        delay_line m_bank_delay[2];
        float m_ear_lag_samples = 0.0f;

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
        std::vector<dump_row> m_dump_rows;
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

    int synthesizer::get_output_sample_rate() const
    {
        return m_implementation->get_output_sample_rate();
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
