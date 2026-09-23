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
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <xmmintrin.h>
//==========================================

// Procedural tire friction: scrub grows into narrow-band squeal as adhesion breaks.

namespace tire_squeal_sound
{
    constexpr float PI     = 3.14159265358979f;
    constexpr float TWO_PI = 6.28318530717959f;

    namespace tuning
    {
        constexpr int sample_rate = 48000;
    }
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
            q    = std::max(q, 0.5f);

            g  = tanf(PI * freq / sample_rate);
            k  = 1.0f / q;
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

            if (fabsf(ic1eq) < 1e-15f)
            {
                ic1eq = 0.0f;
            }
            if (fabsf(ic2eq) < 1e-15f)
            {
                ic2eq = 0.0f;
            }
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

    // one-pole lowpass for parameter smoothing
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
        uint32_t state = 54321;

        float white()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float)state / (float)0xFFFFFFFF * 2.0f - 1.0f;
        }
    };

    // subnormal filter tails cost several times the cpu once the tires are silent
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

    struct debug_data
    {
        float intensity      = 0.0f;
        float speed_norm     = 0.0f;
        float output_level   = 0.0f;
        float output_peak    = 0.0f;
        float tone_level     = 0.0f;
        float screech_level  = 0.0f;
        float body_level     = 0.0f;
        bool  initialized    = false;
    };

    // Several sliding contact regions share a harmonic stick/release waveform.
    // Each moves independently in pitch and pressure, as measured skid recordings
    // do. No recorded samples.
    // initialize runs before any stream pulls; the rest is safe while generate() runs on the
    // audio thread, reset is queued and applied at the start of its next block
    class synthesizer
    {
    public:
        void initialize(int sample_rate = tuning::sample_rate)
        {
            m_sample_rate = static_cast<float>(std::clamp(sample_rate, 8000, 192000));
            m_intensity_smooth.set_cutoff(5.0f, m_sample_rate);
            m_speed_smooth.set_cutoff(4.0f, m_sample_rate);
            m_balance_smooth.set_cutoff(6.0f, m_sample_rate);
            for (int patch = 0; patch < 4; patch++)
            {
                m_pitch_smooth[patch].set_cutoff(7.0f, m_sample_rate);
                m_pressure_smooth[patch].set_cutoff(35.0f, m_sample_rate);
                m_jitter_smooth[patch].set_cutoff(180.0f, m_sample_rate);
            }
            m_body_bp.set_params(650.0f, 0.8f, m_sample_rate);
            m_scrub_bp.set_params(2200.0f, 0.7f, m_sample_rate);
            m_output_hp.set_params(380.0f, 0.7f, m_sample_rate);
            m_output_lp.set_params(3300.0f, 0.7f, m_sample_rate);
            apply_reset();
            m_initialized.store(true, std::memory_order_release);
        }

        // balance is -1 for the left of the screen, 1 for the right, from where the sliding tires are
        void set_parameters(float intensity, float speed_normalized, float balance = 0.0f)
        {
            m_target_intensity.store(std::isfinite(intensity) ? std::clamp(intensity, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
            m_target_speed.store(std::isfinite(speed_normalized) ? std::clamp(speed_normalized, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
            m_target_balance.store(std::isfinite(balance) ? std::clamp(balance, -1.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
        }

        void generate(float* output_buffer, int num_samples, bool stereo = true)
        {
            if (!output_buffer || num_samples <= 0) return;
            if (!m_initialized.load(std::memory_order_acquire))
            {
                std::fill(output_buffer, output_buffer + num_samples * (stereo ? 2 : 1), 0.0f);
                return;
            }
            denormal_guard denormals;
            if (m_reset_requested.exchange(false, std::memory_order_acquire))
            {
                apply_reset();
            }
            const float target_intensity = m_target_intensity.load(std::memory_order_relaxed);
            const float target_speed = m_target_speed.load(std::memory_order_relaxed);
            const float target_balance = m_target_balance.load(std::memory_order_relaxed);
            float tone_sum = 0, scrub_sum = 0, body_sum = 0, output_sum = 0, peak = 0;
            for (int i = 0; i < num_samples; i++)
            {
                float intensity = m_intensity_smooth.process(target_intensity);
                float speed = m_speed_smooth.process(target_speed);
                float tone = 0.0f;
                constexpr float ratios[4] = { 0.93f, 1.02f, 1.13f, 1.39f };
                constexpr float weights[4] = { 0.40f, 0.32f, 0.18f, 0.10f };
                for (int patch = 0; patch < 4; patch++)
                {
                    if (--m_pitch_count[patch] <= 0)
                    {
                        m_pitch_target[patch] = m_noise.white() * 0.16f;
                        m_pitch_count[patch] = static_cast<int>(m_sample_rate * (0.035f + (m_noise.white() + 1.0f) * 0.045f));
                    }
                    if (--m_pressure_count[patch] <= 0)
                    {
                        m_pressure_target[patch] = m_noise.white();
                        m_pressure_count[patch] = static_cast<int>(m_sample_rate * (0.009f + (m_noise.white() + 1.0f) * 0.018f));
                    }
                    float drift = m_pitch_smooth[patch].process(m_pitch_target[patch]);
                    float pressure = m_pressure_smooth[patch].process(m_pressure_target[patch]);
                    float jitter = m_jitter_smooth[patch].process(m_noise.white());
                    float frequency = (940.0f + 140.0f * intensity + 35.0f * speed) * ratios[patch] * (1.0f + drift + jitter * 0.09f);
                    m_phase[patch] += TWO_PI * frequency / m_sample_rate;
                    if (m_phase[patch] >= TWO_PI) m_phase[patch] -= TWO_PI;
                    float phase = m_phase[patch];
                    // Phase-linked harmonics form the asymmetric rubber release.
                    // Their moving sidebands are missing from bandpass white noise.
                    float release = sinf(phase);
                    for (int harmonic = 2; harmonic <= 4; harmonic++)
                    {
                        constexpr float levels[3] = { 0.45f, 0.12f, 0.035f };
                        float nyquist_fade = std::clamp((m_sample_rate * 0.47f - frequency * harmonic) / (m_sample_rate * 0.08f), 0.0f, 1.0f);
                        release += sinf(phase * harmonic + 0.35f * (harmonic - 1)) * levels[harmonic - 2] * (1.0f + pressure * 0.4f) * nyquist_fade;
                    }
                    tone += release * weights[patch] * (0.75f + pressure * 0.35f);
                }
                float noise = m_noise.white();
                float body = m_body_bp.bandpass(noise);
                float scrub = m_scrub_bp.bandpass(noise);
                // the caller already maps intensity to onset at the force peak, so there is one curve
                float mix = tone * 0.85f + scrub * (0.16f - intensity * 0.05f) + body * 0.17f;
                mix = m_output_lp.lowpass(m_output_hp.highpass(mix));
                // One envelope in the synth; the source applies only the mix gain.
                float output = tanhf(mix) * 0.55f * intensity;
                if (intensity < 1e-5f) output = 0.0f;
                tone_sum += tone * tone;
                scrub_sum += scrub * scrub;
                body_sum += body * body;
                output_sum += output * output;
                peak = std::max(peak, fabsf(output));
                if (stereo)
                {
                    // equal power lean toward the side of the car that is sliding, the source still
                    // places the car as a whole
                    float balance = m_balance_smooth.process(target_balance);
                    output_buffer[i * 2]     = output * sqrtf(1.0f - balance);
                    output_buffer[i * 2 + 1] = output * sqrtf(1.0f + balance);
                }
                else output_buffer[i] = output;
            }
            float inv_n = 1.0f / static_cast<float>(num_samples);
            m_debug.intensity = m_intensity_smooth.z1;
            m_debug.speed_norm = m_speed_smooth.z1;
            m_debug.tone_level = sqrtf(tone_sum * inv_n);
            m_debug.screech_level = sqrtf(scrub_sum * inv_n);
            m_debug.body_level = sqrtf(body_sum * inv_n);
            m_debug.output_level = sqrtf(output_sum * inv_n);
            m_debug.output_peak = peak;
            // never wait on a reader, a skipped publish is replaced by the next block
            std::unique_lock<std::mutex> lock(m_debug_mutex, std::try_to_lock);
            if (lock.owns_lock())
            {
                m_debug_published = m_debug;
            }
        }

        void reset()
        {
            m_reset_requested.store(true, std::memory_order_release);
        }

        bool is_initialized() const { return m_initialized.load(std::memory_order_acquire); }
        debug_data get_debug() const
        {
            std::lock_guard<std::mutex> lock(m_debug_mutex);
            return m_debug_published;
        }

    private:
        void apply_reset()
        {
            for (int patch = 0; patch < 4; patch++)
            {
                m_pitch_smooth[patch].reset();
                m_pressure_smooth[patch].reset();
                m_jitter_smooth[patch].reset();
                m_phase[patch] = static_cast<float>(patch);
                m_pitch_count[patch] = m_pressure_count[patch] = 0;
                m_pitch_target[patch] = m_pressure_target[patch] = 0.0f;
            }
            m_body_bp.reset();
            m_scrub_bp.reset();
            m_output_hp.reset();
            m_output_lp.reset();
            m_intensity_smooth.reset();
            m_speed_smooth.reset();
            m_balance_smooth.z1 = m_target_balance.load(std::memory_order_relaxed);
            m_noise = noise_gen();
            m_debug = debug_data();
            m_debug.initialized = true;
        }

        std::atomic<bool> m_initialized { false };
        std::atomic<bool> m_reset_requested { false };
        float m_sample_rate = tuning::sample_rate;
        std::atomic<float> m_target_intensity { 0.0f };
        std::atomic<float> m_target_speed { 0.0f };
        std::atomic<float> m_target_balance { 0.0f };
        one_pole m_balance_smooth;
        mutable std::mutex m_debug_mutex;
        debug_data m_debug_published;
        float m_phase[4] = {}, m_pitch_target[4] = {}, m_pressure_target[4] = {};
        int m_pitch_count[4] = {}, m_pressure_count[4] = {};
        one_pole m_pitch_smooth[4], m_pressure_smooth[4], m_jitter_smooth[4];
        svf_filter m_body_bp, m_scrub_bp, m_output_hp, m_output_lp;
        one_pole m_intensity_smooth, m_speed_smooth;
        noise_gen m_noise;
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

    inline void set_parameters(float intensity, float speed_normalized, float balance = 0.0f)
    {
        get_synthesizer().set_parameters(intensity, speed_normalized, balance);
    }

    inline void generate(float* buffer, int num_samples, bool stereo = true)
    {
        get_synthesizer().generate(buffer, num_samples, stereo);
    }

    inline void reset()
    {
        get_synthesizer().reset();
    }

    inline debug_data get_debug()
    {
        return get_synthesizer().get_debug();
    }
}
