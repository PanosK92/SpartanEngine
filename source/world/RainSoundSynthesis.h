/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===============
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <xmmintrin.h>
//==========================

// Procedural rain. What the ear calls rain is thousands of tiny impacts per second that fuse into a
// hiss, with the few drops that land close by standing out as ticks, and the ones that hit standing
// water ringing as a short rising "plink" (the air bubble they trap resonates, minnaert 1933). Heavy
// rain adds a low roar from the whole landscape. Under a roof the high end is gone and the roof
// itself drums. No recorded samples.

namespace rain_sound
{
    constexpr float PI     = 3.14159265358979f;
    constexpr float TWO_PI = 6.28318530717959f;

    struct svf
    {
        float ic1eq = 0.0f, ic2eq = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;

        void set(float freq, float q, float sample_rate)
        {
            freq     = std::clamp(freq, 20.0f, sample_rate * 0.45f);
            float g  = tanf(PI * freq / sample_rate);
            k        = 1.0f / std::max(q, 0.5f);
            a1       = 1.0f / (1.0f + g * (g + k));
            a2       = g * a1;
            a3       = g * a2;
        }

        void process(float input, float& lp, float& bp, float& hp)
        {
            float v3 = input - ic2eq;
            float v1 = a1 * ic1eq + a2 * v3;
            float v2 = ic2eq + a2 * ic1eq + a3 * v3;
            ic1eq    = 2.0f * v1 - ic1eq;
            ic2eq    = 2.0f * v2 - ic2eq;
            lp       = v2;
            bp       = v1;
            hp       = input - k * v1 - v2;
        }

        float lowpass(float input)  { float lp, bp, hp; process(input, lp, bp, hp); return lp; }
        float bandpass(float input) { float lp, bp, hp; process(input, lp, bp, hp); return bp; }
        float highpass(float input) { float lp, bp, hp; process(input, lp, bp, hp); return hp; }
        void reset()                { ic1eq = ic2eq = 0.0f; }
    };

    struct noise
    {
        uint32_t state = 22695477u;

        float white()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return static_cast<float>(state) / static_cast<float>(0xFFFFFFFF) * 2.0f - 1.0f;
        }

        float uniform() { return white() * 0.5f + 0.5f; }
    };

    struct denormal_guard
    {
        unsigned int saved = _mm_getcsr();
        denormal_guard()  { _mm_setcsr(saved | 0x8040); }
        ~denormal_guard() { _mm_setcsr(saved); }
    };

    // one impact, a burst of noise through its own resonance, or a pitched bubble for a plink
    struct drop
    {
        float amplitude = 0.0f;
        float decay     = 0.0f; // per sample multiplier
        float pan_l     = 0.0f;
        float pan_r     = 0.0f;
        float phase     = 0.0f;
        float frequency = 0.0f; // bubble pitch in hz, 0 for a noise tick
        float chirp     = 0.0f; // per sample pitch multiplier, a bubble rises as it collapses
        svf   filter;
    };

    class synthesizer
    {
    public:
        void initialize(int sample_rate)
        {
            m_sample_rate = static_cast<float>(std::clamp(sample_rate, 8000, 192000));
            m_hiss_hp_l.set(450.0f, 0.7f, m_sample_rate);
            m_hiss_hp_r.set(450.0f, 0.7f, m_sample_rate);
            m_hiss_lp_l.set(8500.0f, 0.6f, m_sample_rate);
            m_hiss_lp_r.set(8500.0f, 0.6f, m_sample_rate);
            m_roar_l.set(220.0f, 0.7f, m_sample_rate);
            m_roar_r.set(240.0f, 0.7f, m_sample_rate);
            m_drum.set(140.0f, 1.4f, m_sample_rate);
            m_noise_l.state = 22695477u;
            m_noise_r.state = 1103515245u;
            m_noise_drop.state = 2654435761u;
            m_smooth = 1.0f - expf(-TWO_PI * 2.0f / m_sample_rate);
            m_initialized.store(true, std::memory_order_release);
        }

        // intensity 0 dry to 1 downpour, shelter 0 open sky to 1 under a roof
        void set_parameters(float intensity, float shelter)
        {
            m_target_intensity.store(std::isfinite(intensity) ? std::clamp(intensity, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
            m_target_shelter.store(std::isfinite(shelter) ? std::clamp(shelter, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
        }

        void generate(float* output, int frames)
        {
            if (!output || frames <= 0)
                return;

            if (!m_initialized.load(std::memory_order_acquire))
            {
                std::fill(output, output + frames * 2, 0.0f);
                return;
            }

            denormal_guard denormals;
            const float target_intensity = m_target_intensity.load(std::memory_order_relaxed);
            const float target_shelter   = m_target_shelter.load(std::memory_order_relaxed);

            // the muffling filter follows shelter per block, it moves far slower than the audio
            float cutoff = 8500.0f - 7200.0f * m_shelter;
            m_hiss_lp_l.set(cutoff, 0.6f, m_sample_rate);
            m_hiss_lp_r.set(cutoff, 0.6f, m_sample_rate);

            for (int i = 0; i < frames; i++)
            {
                m_intensity += (target_intensity - m_intensity) * m_smooth;
                m_shelter   += (target_shelter - m_shelter) * m_smooth;
                const float intensity = m_intensity;
                const float open      = 1.0f - m_shelter;

                // the fused hiss, decorrelated per ear so it surrounds the listener instead of sitting in the head
                float hiss_l = m_hiss_lp_l.lowpass(m_hiss_hp_l.highpass(m_noise_l.white()));
                float hiss_r = m_hiss_lp_r.lowpass(m_hiss_hp_r.highpass(m_noise_r.white()));
                float hiss_gain = powf(intensity, 0.7f) * (0.32f + 0.2f * open);

                // distant roar of a whole landscape being hit, only a real downpour has it
                float roar_gain = intensity * intensity * 0.5f;
                float roar_l    = m_roar_l.lowpass(m_noise_l.white()) * roar_gain;
                float roar_r    = m_roar_r.lowpass(m_noise_r.white()) * roar_gain;

                // roof drumming, a low resonant thud train when sheltered
                float drum = m_drum.bandpass(m_noise_drop.white()) * m_shelter * powf(intensity, 0.8f) * 0.9f;

                float left  = hiss_l * hiss_gain + roar_l + drum;
                float right = hiss_r * hiss_gain + roar_r + drum;

                // individual drops, poisson arrivals, a few close ones stand out of the hiss
                float rate = (40.0f + 1400.0f * intensity) / m_sample_rate;
                if (intensity > 0.001f && m_noise_drop.uniform() < rate)
                {
                    spawn_drop(intensity, open);
                }

                for (drop& d : m_drops)
                {
                    if (d.amplitude < 1e-4f)
                        continue;

                    float sample = 0.0f;
                    if (d.frequency > 0.0f)
                    {
                        d.phase     += TWO_PI * d.frequency / m_sample_rate;
                        d.frequency *= d.chirp;
                        if (d.phase > TWO_PI) d.phase -= TWO_PI;
                        sample       = sinf(d.phase) * d.amplitude;
                    }
                    else
                    {
                        sample = d.filter.bandpass(m_noise_drop.white()) * d.amplitude;
                    }
                    d.amplitude *= d.decay;
                    left        += sample * d.pan_l;
                    right       += sample * d.pan_r;
                }

                output[i * 2]     = tanhf(left) * 0.7f;
                output[i * 2 + 1] = tanhf(right) * 0.7f;
            }
        }

    private:
        void spawn_drop(float intensity, float open)
        {
            drop& d = m_drops[m_next_drop];
            m_next_drop = (m_next_drop + 1) % drop_count;

            // loudness is heavily skewed, most drops are far away and only a few land right next to you
            float closeness = m_noise_drop.uniform();
            closeness       = closeness * closeness * closeness;
            float pan       = m_noise_drop.white();
            d.pan_l         = sqrtf(0.5f * (1.0f - pan));
            d.pan_r         = sqrtf(0.5f * (1.0f + pan));
            d.phase         = 0.0f;

            // a share of the drops hit standing water and ring, more of them once puddles are about
            bool plink = open > 0.3f && m_noise_drop.uniform() < 0.08f + 0.1f * intensity;
            if (plink)
            {
                d.frequency = 1400.0f + 3200.0f * m_noise_drop.uniform();
                d.chirp     = 1.0f + (2.0f + 3.0f * m_noise_drop.uniform()) / m_sample_rate;
                d.amplitude = (0.05f + 0.35f * closeness) * open;
                d.decay     = expf(-1.0f / (m_sample_rate * (0.006f + 0.012f * m_noise_drop.uniform())));
            }
            else
            {
                // under a roof the ticks lose their top and become the patter on the sheet above
                float centre = (1200.0f + 5500.0f * m_noise_drop.uniform()) * (1.0f - 0.75f * (1.0f - open));
                d.frequency  = 0.0f;
                d.filter.reset();
                d.filter.set(centre, 1.5f + 3.0f * m_noise_drop.uniform(), m_sample_rate);
                d.amplitude  = (0.08f + 0.9f * closeness) * (0.6f + 0.4f * open);
                d.decay      = expf(-1.0f / (m_sample_rate * (0.0015f + 0.006f * m_noise_drop.uniform())));
            }
        }

        static constexpr int drop_count = 48;

        std::atomic<bool>  m_initialized      { false };
        std::atomic<float> m_target_intensity { 0.0f };
        std::atomic<float> m_target_shelter   { 0.0f };
        float m_sample_rate = 48000.0f;
        float m_smooth      = 0.0f;
        float m_intensity   = 0.0f;
        float m_shelter     = 0.0f;
        svf   m_hiss_hp_l, m_hiss_hp_r, m_hiss_lp_l, m_hiss_lp_r, m_roar_l, m_roar_r, m_drum;
        noise m_noise_l, m_noise_r, m_noise_drop;
        drop  m_drops[drop_count];
        int   m_next_drop = 0;
    };

    inline synthesizer& get_synthesizer()
    {
        static synthesizer instance;
        return instance;
    }
}
