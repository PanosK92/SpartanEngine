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

// procedural tire squeal synthesizer
// the core idea: tire squeal is not a pitched tone, it's noise shaped by distortion.
// white noise is hard-clipped and bandpass-filtered in the 1-5 khz range to produce
// the harsh, abrasive friction character of rubber scraping on asphalt.
// intensity controls distortion amount and spectral brightness.

namespace tire_squeal_sound
{
    constexpr float PI     = 3.14159265358979f;
    constexpr float TWO_PI = 6.28318530717959f;

    namespace tuning
    {
        constexpr int sample_rate = 48000;

        // main screech band (hz) - slightly lower than pure sandpaper for rubber weight
        constexpr float screech_freq_low     = 1400.0f;
        constexpr float screech_freq_high    = 3000.0f;

        // upper brightness edge
        constexpr float sibilance_freq_low   = 2800.0f;
        constexpr float sibilance_freq_high  = 5000.0f;

        // body - modest low-mid weight, not enough to become windy
        constexpr float body_freq_low        = 500.0f;
        constexpr float body_freq_high       = 900.0f;

        // layer levels
        constexpr float screech_level        = 0.55f;
        constexpr float sibilance_level      = 0.20f;
        constexpr float body_level           = 0.25f;

        // screech distortion - warm multi-stage tanh, not crispy hard-clip
        constexpr float screech_drive_min    = 3.0f;
        constexpr float screech_drive_max    = 8.0f;

        // parameter smoothing (hz)
        constexpr float intensity_smoothing  = 10.0f;
        constexpr float speed_smoothing      = 6.0f;
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
        float screech_level  = 0.0f;
        float sibilance_lvl  = 0.0f;
        float body_level     = 0.0f;
        bool  initialized    = false;
    };

    class synthesizer
    {
    public:
        void initialize(int sample_rate = tuning::sample_rate)
        {
            m_sample_rate = (float)sample_rate;

            // screech band
            m_screech_pre_bp.set_params(2000.0f, 1.8f, m_sample_rate);
            m_screech_post_bp.set_params(2000.0f, 1.2f, m_sample_rate);

            // sibilance
            m_sibilance_hp.set_params(2800.0f, 0.8f, m_sample_rate);
            m_sibilance_post_lp.set_params(5000.0f, 0.8f, m_sample_rate);

            // body - single band, modest contribution
            m_body_bp.set_params(700.0f, 1.0f, m_sample_rate);

            // output filters
            m_output_hp.set_params(350.0f, 0.7f, m_sample_rate);
            m_output_lp.set_params(8500.0f, 0.7f, m_sample_rate);

            // parameter smoothing
            m_intensity_smooth.set_cutoff(tuning::intensity_smoothing, m_sample_rate);
            m_speed_smooth.set_cutoff(tuning::speed_smoothing, m_sample_rate);

            m_initialized = true;
            m_debug.initialized = true;
        }

        void set_parameters(float intensity, float speed_normalized)
        {
            m_target_intensity  = std::clamp(intensity, 0.0f, 1.0f);
            m_target_speed_norm = std::clamp(speed_normalized, 0.0f, 1.0f);
        }

        void generate(float* output_buffer, int num_samples, bool stereo = true)
        {
            if (!m_initialized)
            {
                int total = stereo ? num_samples * 2 : num_samples;
                for (int i = 0; i < total; i++)
                    output_buffer[i] = 0.0f;
                return;
            }

            float screech_sum = 0.0f, sibilance_sum = 0.0f, body_sum = 0.0f;
            float output_sum = 0.0f, peak = 0.0f;

            for (int i = 0; i < num_samples; i++)
            {
                float intensity  = m_intensity_smooth.process(m_target_intensity);
                float speed_norm = m_speed_smooth.process(m_target_speed_norm);

                if (intensity < 0.005f)
                {
                    if (stereo)
                    {
                        output_buffer[i * 2]     = 0.0f;
                        output_buffer[i * 2 + 1] = 0.0f;
                    }
                    else
                    {
                        output_buffer[i] = 0.0f;
                    }
                    continue;
                }

                float noise_w  = m_noise.white();
                float noise_w2 = m_noise.white();
                float noise_p  = m_noise.pink();

                // ---- screech layer ----
                // white noise -> bandpass -> warm multi-stage saturation -> bandpass
                // warm tanh saturation instead of hard-clipping avoids the dry/papery character
                float screech_freq = tuning::screech_freq_low +
                    (tuning::screech_freq_high - tuning::screech_freq_low) * (speed_norm * 0.4f + intensity * 0.6f);
                m_screech_pre_bp.set_params(screech_freq, 1.8f + intensity * 0.5f, m_sample_rate);
                float screech = m_screech_pre_bp.bandpass(noise_w);

                float drive = tuning::screech_drive_min +
                    (tuning::screech_drive_max - tuning::screech_drive_min) * intensity;

                // stage 1: moderate tanh saturation
                screech = tanhf(screech * drive);

                // stage 2: softer saturation pass to compress and thicken
                screech = screech * 1.8f / (1.0f + fabsf(screech) * 0.6f);

                // post-filter keeps the spectral energy focused
                m_screech_post_bp.set_params(screech_freq * 1.05f, 1.0f, m_sample_rate);
                screech = m_screech_post_bp.bandpass(screech);

                // ---- sibilance layer ----
                float sib_freq = tuning::sibilance_freq_low +
                    (tuning::sibilance_freq_high - tuning::sibilance_freq_low) * intensity;
                m_sibilance_hp.set_params(sib_freq * 0.8f, 0.8f, m_sample_rate);
                float sibilance = m_sibilance_hp.highpass(noise_w2);
                sibilance = tanhf(sibilance * (2.0f + intensity * 2.5f));

                m_sibilance_post_lp.set_params(sib_freq, 0.7f, m_sample_rate);
                sibilance = m_sibilance_post_lp.lowpass(sibilance);

                // ---- body layer ----
                // modest low-mid weight so it doesn't sound thin, but not enough to get windy
                float body_freq = tuning::body_freq_low +
                    (tuning::body_freq_high - tuning::body_freq_low) * speed_norm;
                m_body_bp.set_params(body_freq, 1.0f, m_sample_rate);
                float body = m_body_bp.bandpass(noise_p);
                body = tanhf(body * (2.0f + intensity * 2.0f));

                // ---- mix ----
                float output = 0.0f;
                output += screech   * tuning::screech_level;
                output += sibilance * tuning::sibilance_level;
                output += body      * tuning::body_level;

                // debug accumulation
                screech_sum   += screech * screech;
                sibilance_sum += sibilance * sibilance;
                body_sum      += body * body;

                // amplitude envelope
                float envelope = intensity * intensity;
                output *= envelope;

                // random micro-variation (not periodic)
                output *= 0.88f + m_noise.white() * 0.12f;

                // output processing
                output = m_dc_blocker.process(output);
                output = m_output_hp.highpass(output);

                // final soft limiter
                output = tanhf(output * 1.3f) * 0.85f;
                output = m_output_lp.lowpass(output);

                output *= 0.7f;

                output_sum += output * output;
                if (fabsf(output) > peak) peak = fabsf(output);

                if (stereo)
                {
                    float stereo_diff = m_noise.white() * 0.02f;
                    output_buffer[i * 2]     = output * (1.0f + stereo_diff);
                    output_buffer[i * 2 + 1] = output * (1.0f - stereo_diff);
                }
                else
                {
                    output_buffer[i] = output;
                }
            }

            // update debug data
            float inv_n = 1.0f / (float)num_samples;
            m_debug.intensity     = m_intensity_smooth.z1;
            m_debug.speed_norm    = m_speed_smooth.z1;
            m_debug.screech_level = sqrtf(screech_sum * inv_n);
            m_debug.sibilance_lvl = sqrtf(sibilance_sum * inv_n);
            m_debug.body_level    = sqrtf(body_sum * inv_n);
            m_debug.output_level  = sqrtf(output_sum * inv_n);
            m_debug.output_peak   = peak;
        }

        void reset()
        {
            m_screech_pre_bp.reset();
            m_screech_post_bp.reset();
            m_sibilance_hp.reset();
            m_sibilance_post_lp.reset();
            m_body_bp.reset();
            m_output_hp.reset();
            m_output_lp.reset();
            m_dc_blocker.reset();
            m_intensity_smooth.reset();
            m_speed_smooth.reset();
        }

        bool is_initialized() const { return m_initialized; }
        const debug_data& get_debug() const { return m_debug; }

    private:
        bool  m_initialized = false;
        float m_sample_rate = tuning::sample_rate;

        float m_target_intensity  = 0.0f;
        float m_target_speed_norm = 0.0f;

        // screech: noise -> bandpass -> hard clip -> bandpass
        svf_filter m_screech_pre_bp;
        svf_filter m_screech_post_bp;

        // sibilance: noise -> highpass -> soft clip -> lowpass
        svf_filter m_sibilance_hp;
        svf_filter m_sibilance_post_lp;

        // body: pink noise -> bandpass -> saturation
        svf_filter m_body_bp;

        // output
        svf_filter m_output_hp;
        svf_filter m_output_lp;
        dc_blocker m_dc_blocker;

        // parameter smoothing
        one_pole m_intensity_smooth;
        one_pole m_speed_smooth;

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
