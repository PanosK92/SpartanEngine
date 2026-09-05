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
#include <cstdint>
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

        // pink noise (paul kellet approximation)
        float pb0 = 0, pb1 = 0, pb2 = 0, pb3 = 0, pb4 = 0, pb5 = 0, pb6 = 0;
        float pink()
        {
            float w = white();
            pb0 = 0.99886f * pb0 + w * 0.0555179f;
            pb1 = 0.99332f * pb1 + w * 0.0750759f;
            pb2 = 0.96900f * pb2 + w * 0.1538520f;
            pb3 = 0.86650f * pb3 + w * 0.3104856f;
            pb4 = 0.55000f * pb4 + w * 0.5329522f;
            pb5 = -0.7616f * pb5 - w * 0.0168980f;
            float out = pb0 + pb1 + pb2 + pb3 + pb4 + pb5 + pb6 + w * 0.5362f;
            pb6 = w * 0.115926f;
            return out * 0.11f;
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
    // Each moves independently in pitch and pressure, as in the measured skid
    // references listed in tools/audio_tests/README.md. No recorded samples.
    class synthesizer
    {
    public:
        void initialize(int sample_rate = tuning::sample_rate)
        {
            m_sample_rate = static_cast<float>(std::clamp(sample_rate, 8000, 192000));
            m_intensity_smooth.set_cutoff(5.0f, m_sample_rate);
            m_speed_smooth.set_cutoff(4.0f, m_sample_rate);
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
            m_initialized = true;
            reset();
        }

        void set_parameters(float intensity, float speed_normalized)
        {
            m_target_intensity = std::isfinite(intensity) ? std::clamp(intensity, 0.0f, 1.0f) : 0.0f;
            m_target_speed = std::isfinite(speed_normalized) ? std::clamp(speed_normalized, 0.0f, 1.0f) : 0.0f;
        }

        void generate(float* output_buffer, int num_samples, bool stereo = true)
        {
            if (!output_buffer || num_samples <= 0) return;
            if (!m_initialized)
            {
                std::fill(output_buffer, output_buffer + num_samples * (stereo ? 2 : 1), 0.0f);
                return;
            }
            float tone_sum = 0, scrub_sum = 0, body_sum = 0, output_sum = 0, peak = 0;
            for (int i = 0; i < num_samples; i++)
            {
                float intensity = m_intensity_smooth.process(m_target_intensity);
                float speed = m_speed_smooth.process(m_target_speed);
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
                float onset = std::clamp((intensity - 0.08f) / 0.55f, 0.0f, 1.0f);
                onset = onset * onset * (3.0f - 2.0f * onset);
                tone *= onset;
                float mix = tone * 0.85f + scrub * (0.16f - onset * 0.05f) + body * 0.17f;
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
                    // Position is supplied by the audio source, not sample-wise pan noise.
                    output_buffer[i * 2] = output_buffer[i * 2 + 1] = output;
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
        }

        void reset()
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
            m_noise = noise_gen();
            m_target_intensity = m_target_speed = 0.0f;
            m_debug = debug_data();
            m_debug.initialized = m_initialized;
        }

        bool is_initialized() const { return m_initialized; }
        const debug_data& get_debug() const { return m_debug; }

    private:
        bool m_initialized = false;
        float m_sample_rate = tuning::sample_rate;
        float m_target_intensity = 0.0f;
        float m_target_speed = 0.0f;
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

    inline void set_parameters(float intensity, float speed_normalized)
    {
        get_synthesizer().set_parameters(intensity, speed_normalized);
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
}
