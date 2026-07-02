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

// exhaust audio pipeline ported from engine-sim by ange yaghi, mit license
// https://github.com/ange-yaghi/engine-sim
// exhaust impulse response: smooth_05.wav from engine-sim sound library

#pragma once

//= INCLUDES ===============================
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <SDL3/SDL_audio.h>
#include "CarTireSquealSynthesis.h"
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

        // exhaust convolution path (engine-sim style)
        // 1024 taps at 48k = ~21 ms tail, short enough to avoid bathroom-room reverb character
        constexpr int   convolution_taps   = 1024;
        // 0.15 keeps firing transients prominent in the convolution input, gives the synth its bite
        constexpr float df_f_mix           = 0.15f;
        constexpr float air_noise          = 0.04f;
        constexpr float convolution_wet    = 1.0f;
        // 1 ms haas-style offset on right bank, just enough for stereo width
        constexpr float stereo_offset      = 0.001f;
        // bleed across banks so neither channel ever drops out, 0 = hard split, 1 = mono
        constexpr float bank_cross_bleed   = 0.35f;
        // ir has mic/cabinet resonances out to 20 khz, gentle lp at load to kill 16-20 khz junk
        // 9 khz / 2 poles preserves engine snarl in the 2-8 khz band, only nukes the inaudible buzz
        constexpr float ir_lowpass_hz      = 9000.0f;
        constexpr int   ir_lowpass_poles   = 2;

        // tames the ir's secondary 833 hz peak which gets blasted by the firing harmonics at high rpm
        // (without it the 2nd harmonic of the firing fundamental at 4000 rpm lands exactly on the resonance)
        constexpr float exhaust_notch_hz   = 830.0f;
        constexpr float exhaust_notch_q    = 2.5f;
        constexpr float exhaust_notch_depth = 0.5f;

        // output dc blocker pole, 0.998 = 15 hz corner, tighter than per-bank blocker
        constexpr float output_dc_blocker_r = 0.998f;

        // limiter-style auto leveler with asymmetric slew (slow attack on gain rise, fast catch on transients)
        constexpr float leveler_target     = 0.7f;
        constexpr float leveler_max_gain   = 2.2f;
        constexpr float leveler_min_gain   = 0.05f;
        constexpr float leveler_attack     = 800.0f;
        constexpr float leveler_release    = 4.0f;
        // gain slew per sample: down (catch transients) fast, up (no pumping in quiet bits) slow
        constexpr float leveler_gain_down  = 0.012f;
        constexpr float leveler_gain_up    = 0.0006f;

        // soft saturation knee, replaces hard clamp at +-1 to avoid clicks during transients
        // the knee hardens with throttle and rpm so the engine roars under load, makeup gain
        // keeps the nominal level constant so only the saturation density changes
        constexpr float output_soft_drive     = 1.15f;
        constexpr float output_drive_throttle = 0.9f;
        constexpr float output_drive_rpm      = 0.45f;

        // idle lope, slow random rpm hunting when the throttle is closed near idle
        constexpr float idle_lope_rpm     = 25.0f;
        constexpr float idle_lope_rate_hz = 2.0f;

        // exhaust ir asset (the _short variant has had its 250ms pre-silence trimmed
        // and a clean exponential decay tail applied, see binaries/trim_ir.py)
        constexpr const char* exhaust_ir_path = "project\\music\\exhaust_ir_short.wav";

        // layer mix levels
        constexpr float combustion_level   = 0.18f;
        constexpr float exhaust_level      = 1.0f;
        constexpr float mechanical_level   = 0.12f;
        constexpr float induction_level    = 0.05f;

        // throttle below this at revs counts as overrun
        constexpr float overrun_threshold  = 0.15f;

        // overrun afterfire, random firings bang (unburnt fuel igniting in the hot pipes) or
        // miss (fuel cut) so lift-off burbles through the same distorted exhaust chain
        constexpr float afterfire_chance    = 0.12f;
        constexpr float afterfire_intensity = 3.5f;
        constexpr float misfire_chance      = 0.25f;
        constexpr float misfire_intensity   = 0.15f;

        // turbocharger
        constexpr float turbo_spool_up     = 2.5f;
        constexpr float turbo_spool_down   = 1.8f;
        constexpr float turbo_min_rpm      = 2500.0f;
        constexpr float turbo_full_rpm     = 6000.0f;

        // flutter/surge
        constexpr float flutter_freq       = 22.0f;
        constexpr float flutter_decay      = 3.0f;

        // wastegate
        constexpr float wastegate_decay    = 3.0f;

        // turbo mix levels
        constexpr float turbo_whine_level   = 0.06f;
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
        float waveform_cyl[waveform_size]  = {};
        float waveform_vin[waveform_size]  = {};
        float waveform_exh[waveform_size]  = {};
        int   waveform_write_pos           = 0;

        // log-magnitude spectrum, fft_size/2 bins covering 0..nyquist
        static constexpr int fft_size      = 512;
        static constexpr int spectrum_bins = fft_size / 2;
        float spectrum[spectrum_bins]      = {};

        // leveler state
        float leveler_gain     = 1.0f;
        float leveler_envelope = 0.0f;

        // wav dump state, 0 = idle, >0 = capturing/done
        int   dump_total      = 0;
        int   dump_progress   = 0;
        bool  dump_ready      = false;

        uint64_t generate_calls    = 0;
        uint64_t samples_generated = 0;
        bool initialized           = false;
        int  ir_taps               = 0;
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

    // dc blocker, default corner ~38 hz at 48k, set_r() lets the output stage tighten this
    struct dc_blocker
    {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float r  = 0.995f;

        void set_r(float r_) { r = r_; }

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

    // single-sample derivative
    struct derivative_filter
    {
        float prev = 0.0f;
        float dt   = 1.0f / (float)tuning::sample_rate;

        void set_sample_rate(float sample_rate)
        {
            dt = 1.0f / sample_rate;
        }

        float process(float input)
        {
            float out = (input - prev) / dt;
            prev = input;
            return out;
        }

        void reset() { prev = 0.0f; }
    };

    // iterative radix-2 cooley-tukey fft, in-place on real/imag buffers, n must be power of two
    inline void fft_radix2(float* re, float* im, int n)
    {
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j)
            {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            float ang = -TWO_PI / (float)len;
            float wlen_re = cosf(ang);
            float wlen_im = sinf(ang);
            int half = len >> 1;
            for (int i = 0; i < n; i += len)
            {
                float w_re = 1.0f, w_im = 0.0f;
                for (int k = 0; k < half; ++k)
                {
                    float u_re = re[i + k];
                    float u_im = im[i + k];
                    float v_re = re[i + k + half] * w_re - im[i + k + half] * w_im;
                    float v_im = re[i + k + half] * w_im + im[i + k + half] * w_re;
                    re[i + k]        = u_re + v_re;
                    im[i + k]        = u_im + v_im;
                    re[i + k + half] = u_re - v_re;
                    im[i + k + half] = u_im - v_im;
                    float nw_re = w_re * wlen_re - w_im * wlen_im;
                    float nw_im = w_re * wlen_im + w_im * wlen_re;
                    w_re = nw_re;
                    w_im = nw_im;
                }
            }
        }
    }

    // uniform partitioned overlap-save convolution, cost is o(taps/block) complex macs per sample
    // instead of o(taps) real macs, which makes engine-sim's ~10000-tap exhaust ir affordable
    // adds block_size samples of latency (~5 ms at 48 khz), inaudible for engine audio
    struct convolution_filter
    {
        static constexpr int block_size = 256;
        static constexpr int fft_size   = block_size * 2;

        int                partition_count = 0;
        std::vector<float> ir_spectra_re;
        std::vector<float> ir_spectra_im;
        std::vector<float> input_spectra_re;
        std::vector<float> input_spectra_im;
        int                newest_spectrum = 0;
        float              input_block[fft_size]    = {};
        float              output_block[block_size] = {};
        int                block_pos = 0;

        void initialize(const float* impulse, int sample_count)
        {
            partition_count = (sample_count + block_size - 1) / block_size;
            ir_spectra_re.assign((size_t)partition_count * fft_size, 0.0f);
            ir_spectra_im.assign((size_t)partition_count * fft_size, 0.0f);
            input_spectra_re.assign((size_t)partition_count * fft_size, 0.0f);
            input_spectra_im.assign((size_t)partition_count * fft_size, 0.0f);

            float re[fft_size];
            float im[fft_size];
            for (int p = 0; p < partition_count; ++p)
            {
                for (int i = 0; i < fft_size; ++i)
                {
                    int src = p * block_size + i;
                    re[i] = (i < block_size && src < sample_count) ? impulse[src] : 0.0f;
                    im[i] = 0.0f;
                }
                fft_radix2(re, im, fft_size);
                std::copy(re, re + fft_size, ir_spectra_re.begin() + (size_t)p * fft_size);
                std::copy(im, im + fft_size, ir_spectra_im.begin() + (size_t)p * fft_size);
            }

            reset();
        }

        float process(float input)
        {
            if (partition_count == 0)
            {
                return input;
            }

            input_block[block_size + block_pos] = input;
            float out = output_block[block_pos];
            block_pos++;
            if (block_pos == block_size)
            {
                process_block();
                block_pos = 0;
            }
            return out;
        }

        void process_block()
        {
            // fft of the last two blocks (overlap-save)
            float re[fft_size];
            float im[fft_size];
            for (int i = 0; i < fft_size; ++i)
            {
                re[i] = input_block[i];
                im[i] = 0.0f;
            }
            fft_radix2(re, im, fft_size);

            newest_spectrum = (newest_spectrum + partition_count - 1) % partition_count;
            std::copy(re, re + fft_size, input_spectra_re.begin() + (size_t)newest_spectrum * fft_size);
            std::copy(im, im + fft_size, input_spectra_im.begin() + (size_t)newest_spectrum * fft_size);

            // accumulate spectral products, ir partition p pairs with the input block from p blocks ago
            float acc_re[fft_size] = {};
            float acc_im[fft_size] = {};
            for (int p = 0; p < partition_count; ++p)
            {
                const float* xr = &input_spectra_re[(size_t)((newest_spectrum + p) % partition_count) * fft_size];
                const float* xi = &input_spectra_im[(size_t)((newest_spectrum + p) % partition_count) * fft_size];
                const float* hr = &ir_spectra_re[(size_t)p * fft_size];
                const float* hi = &ir_spectra_im[(size_t)p * fft_size];
                for (int k = 0; k < fft_size; ++k)
                {
                    acc_re[k] += xr[k] * hr[k] - xi[k] * hi[k];
                    acc_im[k] += xr[k] * hi[k] + xi[k] * hr[k];
                }
            }

            // inverse fft via conjugation, the second half is the valid overlap-save output
            for (int k = 0; k < fft_size; ++k)
            {
                acc_im[k] = -acc_im[k];
            }
            fft_radix2(acc_re, acc_im, fft_size);
            const float inv_n = 1.0f / (float)fft_size;
            for (int i = 0; i < block_size; ++i)
            {
                output_block[i] = acc_re[block_size + i] * inv_n;
            }

            // slide the current block into the overlap position
            for (int i = 0; i < block_size; ++i)
            {
                input_block[i] = input_block[block_size + i];
            }
        }

        void reset()
        {
            std::fill(input_spectra_re.begin(), input_spectra_re.end(), 0.0f);
            std::fill(input_spectra_im.begin(), input_spectra_im.end(), 0.0f);
            std::fill(std::begin(input_block), std::end(input_block), 0.0f);
            std::fill(std::begin(output_block), std::end(output_block), 0.0f);
            newest_spectrum = 0;
            block_pos = 0;
        }
    };

    // slow auto-gain that keeps stereo balance, tracks max(|l|,|r|)
    struct leveler
    {
        float gain      = 1.0f;
        float envelope  = 0.0f;
        float target    = tuning::leveler_target;
        float gain_min  = tuning::leveler_min_gain;
        float gain_max  = tuning::leveler_max_gain;
        float a_attack  = 0.0f;
        float a_release = 0.0f;

        void set_sample_rate(float sample_rate)
        {
            a_attack  = expf(-tuning::leveler_attack  / sample_rate);
            a_release = expf(-tuning::leveler_release / sample_rate);
        }

        void process(float& l, float& r)
        {
            float peak = std::max(fabsf(l), fabsf(r));
            float a = (peak > envelope) ? a_attack : a_release;
            envelope = peak + a * (envelope - peak);

            float target_gain = (envelope > 1e-6f) ? (target / envelope) : gain_max;
            target_gain = std::clamp(target_gain, gain_min, gain_max);

            // asymmetric slew: drop gain fast to catch transients, raise slowly to avoid pumping up the noise floor
            float slew = (target_gain < gain) ? tuning::leveler_gain_down : tuning::leveler_gain_up;
            gain += (target_gain - gain) * slew;

            l *= gain;
            r *= gain;
        }

        void reset()
        {
            gain = 1.0f;
            envelope = 0.0f;
        }
    };

    // loads a wav file as mono float32 at target_sample_rate, silence-trimmed, energy-normalized, capped to max_taps
    inline std::vector<float> load_impulse_response(const char* path, int max_taps, int target_sample_rate)
    {
        std::vector<float> ir_out;

        SDL_AudioSpec wav_spec = {};
        uint8_t* wav_buffer    = nullptr;
        uint32_t wav_length    = 0;
        if (!SDL_LoadWAV(path, &wav_spec, &wav_buffer, &wav_length))
        {
            return ir_out;
        }

        // resample to running sample rate so conv runs at the correct rate
        SDL_AudioSpec target_spec = {};
        target_spec.freq          = target_sample_rate;
        target_spec.format        = SDL_AUDIO_F32;
        target_spec.channels      = 1;
        uint8_t* target_buffer    = nullptr;
        int target_length         = 0;
        if (!SDL_ConvertAudioSamples(&wav_spec, wav_buffer, (int)wav_length, &target_spec, &target_buffer, &target_length))
        {
            SDL_free(wav_buffer);
            return ir_out;
        }
        SDL_free(wav_buffer);

        const float* samples = reinterpret_cast<const float*>(target_buffer);
        int num_samples = target_length / (int)sizeof(float);

        // trim trailing low-energy tail (engine-sim does the same trick on int16 source)
        const float silence_threshold = 1.0f / 32767.0f * 100.0f;
        int trimmed = 0;
        for (int i = 0; i < num_samples; ++i)
        {
            if (fabsf(samples[i]) > silence_threshold)
            {
                trimmed = i + 1;
            }
        }
        int taps = std::min(max_taps, trimmed > 0 ? trimmed : num_samples);

        ir_out.resize(taps);
        for (int i = 0; i < taps; ++i)
            ir_out[i] = samples[i];

        SDL_free(target_buffer);

        // kill out-of-band resonances baked into the recording (16-20 khz mic/cabinet artifacts)
        // bidirectional one-pole cascade is zero-phase, no extra delay added to the ir
        if (tuning::ir_lowpass_hz > 0 && tuning::ir_lowpass_hz < (float)target_sample_rate * 0.5f && taps > 4)
        {
            float b1 = expf(-TWO_PI * tuning::ir_lowpass_hz / (float)target_sample_rate);
            float a0 = 1.0f - b1;
            for (int pass = 0; pass < tuning::ir_lowpass_poles; ++pass)
            {
                float z = ir_out[0];
                for (int i = 0; i < taps; ++i) { z = ir_out[i] * a0 + z * b1; ir_out[i] = z; }
                z = ir_out[taps - 1];
                for (int i = taps - 1; i >= 0; --i) { z = ir_out[i] * a0 + z * b1; ir_out[i] = z; }
            }
        }

        // null the ir's dc gain by subtracting the mean, otherwise convolution adds a constant offset
        // to the output (the original recording had ~-0.75 sum-of-taps after lp, causing dc drift)
        double mean = 0.0;
        for (float v : ir_out) mean += (double)v;
        mean /= (double)ir_out.size();
        if (fabs(mean) > 1e-9)
        {
            for (float& v : ir_out) v -= (float)mean;
        }

        // l2-energy normalize so convolution gain is ~unity for white-noise input
        // (peak-normalising made the convolution output ~50x too hot, which the leveler
        //  could compensate for but only after destroying transients via tanh)
        double sum_sq = 0.0;
        for (float v : ir_out)
            sum_sq += (double)v * (double)v;
        float energy = (float)sqrt(sum_sq);
        if (energy > 1e-6f)
        {
            float inv = 1.0f / energy;
            for (float& v : ir_out)
                v *= inv;
        }

        return ir_out;
    }

    // cylinder combustion model
    struct cylinder
    {
        float    phase           = 0.0f;
        float    phase_inc       = 0.0f;
        float    firing_offset   = 0.0f;
        float    pressure        = 0.0f;
        float    prev_pressure   = 0.0f;
        bool     is_firing       = false;
        bool     bank_left       = true;
        float    timing_jitter   = 0.0f;
        float    intensity_var   = 1.0f;
        float    cycle_intensity = 1.0f;
        uint32_t rng_state       = 12345;

        // xorshift32, tiny per-cylinder rng so each cyl varies independently across cycles
        float rand01()
        {
            rng_state ^= rng_state << 13;
            rng_state ^= rng_state >> 17;
            rng_state ^= rng_state << 5;
            return (float)rng_state / (float)0xFFFFFFFFu;
        }

        void init(int index, int total_cylinders)
        {
            // v12 firing order (60-degree intervals)
            static const int firing_order_12[] = {0, 6, 4, 10, 2, 8, 5, 11, 1, 7, 3, 9};
            int order_pos = firing_order_12[index % 12];
            firing_offset = (float)order_pos / (float)total_cylinders;

            // banks alternate firings (real v12 behaviour), cyl on even firing positions go left
            bank_left = (order_pos & 1) == 0;

            // per-cylinder variation
            timing_jitter = ((index * 7 + 3) % 17) / 170.0f - 0.05f;
            intensity_var = 0.95f + ((index * 13 + 5) % 11) / 110.0f;

            // independent rng seed per cylinder for cycle-to-cycle variation
            rng_state = 0x9e3779b9u ^ (uint32_t)(index + 1) * 0x85ebca6bu;
        }

        void set_rpm(float rpm, float sample_rate)
        {
            float cycles_per_second = rpm / 60.0f / 2.0f;
            phase_inc = cycles_per_second / sample_rate;
        }

        float tick(float load, float rpm_norm, float overrun)
        {
            phase += phase_inc;
            if (phase >= 1.0f)
            {
                phase -= 1.0f;
            }

            // floor-based wrap, fmodf returns negative values for negative jitter and the
            // envelope code below would then run with t < 0 and produce a garbage pressure blip
            float effective_phase = phase + firing_offset + timing_jitter * (1.0f - load * 0.5f);
            effective_phase -= floorf(effective_phase);
            float window_end = tuning::combustion_attack + tuning::combustion_hold + tuning::combustion_decay;

            if (effective_phase < window_end)
            {
                // roll a new random intensity at window entry so the envelope is never rescaled
                // mid burn, rolling at the shared phase wrap clicked all cylinders at once
                if (!is_firing)
                {
                    cycle_intensity = 0.90f + rand01() * 0.20f;

                    // overrun: each firing either bangs (afterfire) or misses (fuel cut) at random
                    if (overrun > 0.0f)
                    {
                        float r = rand01();
                        if (r < overrun * tuning::afterfire_chance)
                        {
                            cycle_intensity *= tuning::afterfire_intensity;
                        }
                        else if (r < overrun * (tuning::afterfire_chance + tuning::misfire_chance))
                        {
                            cycle_intensity *= tuning::misfire_intensity;
                        }
                    }
                }
                is_firing = true;

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
                pressure = env * load_factor * intensity_var * cycle_intensity;
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
            // phase must reset to zero, the firing offset is added inside tick and
            // resetting to it would apply the offset twice, pairing up the firings
            phase = 0.0f;
            pressure = prev_pressure = 0.0f;
            is_firing = false;
            cycle_intensity = 1.0f;
        }
    };

    // writes 16-bit pcm stereo wav, samples already interleaved float in -1..1
    inline bool write_wav_int16_stereo(const char* path, const float* interleaved, int frames, int sample_rate)
    {
        FILE* f = nullptr;
        fopen_s(&f, path, "wb");
        if (!f)
        {
            return false;
        }

        uint32_t data_bytes  = (uint32_t)(frames * 2 * sizeof(int16_t));
        uint32_t riff_size   = 36 + data_bytes;
        uint16_t channels    = 2;
        uint16_t bits        = 16;
        uint32_t byte_rate   = sample_rate * channels * (bits / 8);
        uint16_t block_align = channels * (bits / 8);

        fwrite("RIFF", 1, 4, f);
        fwrite(&riff_size, 4, 1, f);
        fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f);
        uint32_t fmt_size = 16;
        uint16_t pcm_fmt  = 1;
        fwrite(&fmt_size, 4, 1, f);
        fwrite(&pcm_fmt, 2, 1, f);
        fwrite(&channels, 2, 1, f);
        fwrite(&sample_rate, 4, 1, f);
        fwrite(&byte_rate, 4, 1, f);
        fwrite(&block_align, 2, 1, f);
        fwrite(&bits, 2, 1, f);
        fwrite("data", 1, 4, f);
        fwrite(&data_bytes, 4, 1, f);

        for (int i = 0; i < frames * 2; ++i)
        {
            float s = std::clamp(interleaved[i], -1.0f, 1.0f);
            int16_t v = (int16_t)lroundf(s * 32767.0f);
            fwrite(&v, 2, 1, f);
        }

        fclose(f);
        return true;
    }

    // tunable at runtime, defaults from tuning::*
    struct runtime_params
    {
        float df_f_mix         = tuning::df_f_mix;
        float air_noise        = tuning::air_noise;
        float convolution_wet  = tuning::convolution_wet;
        float combustion_level = tuning::combustion_level;
        float exhaust_level    = tuning::exhaust_level;
        float drive_extra      = 0.0f;
        float leveler_target   = tuning::leveler_target;
        float master_offset    = 0.0f;
        float notch_depth      = tuning::exhaust_notch_depth;
    };

    class synthesizer
    {
    public:
        runtime_params params;

        void initialize(int sample_rate = tuning::sample_rate, const char* ir_path = tuning::exhaust_ir_path)
        {
            m_sample_rate = (float)sample_rate;

            m_cylinders.resize(tuning::cylinder_count);
            for (int i = 0; i < tuning::cylinder_count; i++)
                m_cylinders[i].init(i, tuning::cylinder_count);

            // exhaust convolution path (engine-sim style)
            std::vector<float> ir = load_impulse_response(ir_path, tuning::convolution_taps, (int)m_sample_rate);
            if (!ir.empty())
            {
                m_conv_l.initialize(ir.data(), (int)ir.size());

                // tiny offset on right channel for stereo width
                int offset = (int)(tuning::stereo_offset * m_sample_rate);
                offset = std::clamp(offset, 0, (int)ir.size() - 1);
                std::vector<float> ir_r(ir.size(), 0.0f);
                for (int i = 0; i < (int)ir.size() - offset; ++i)
                    ir_r[i + offset] = ir[i];
                m_conv_r.initialize(ir_r.data(), (int)ir_r.size());

                m_ir_loaded = true;
                m_debug.ir_taps = (int)ir.size();
            }
            else
            {
                m_ir_loaded = false;
                m_debug.ir_taps = 0;
            }

            m_deriv_l.set_sample_rate(m_sample_rate);
            m_deriv_r.set_sample_rate(m_sample_rate);
            m_leveler.set_sample_rate(m_sample_rate);

            // notch filters tame the ir's secondary resonance that high-rpm harmonics excite
            m_exhaust_notch_l.set_params(tuning::exhaust_notch_hz, tuning::exhaust_notch_q, m_sample_rate);
            m_exhaust_notch_r.set_params(tuning::exhaust_notch_hz, tuning::exhaust_notch_q, m_sample_rate);

            // output dc blockers run tighter than the pre-conv ones to fully kill drift
            m_dc_block_out_l.set_r(tuning::output_dc_blocker_r);
            m_dc_block_out_r.set_r(tuning::output_dc_blocker_r);

            // parameter smoothing
            m_rpm_smooth.set_cutoff(8.0f, m_sample_rate);
            m_throttle_smooth.set_cutoff(15.0f, m_sample_rate);
            m_load_smooth.set_cutoff(10.0f, m_sample_rate);
            m_lope_smooth.set_cutoff(1.5f, m_sample_rate);

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
                // idle lope, slow random rpm hunting so a closed-throttle idle breathes
                if (--m_lope_countdown <= 0)
                {
                    m_lope_countdown = (int)(m_sample_rate / tuning::idle_lope_rate_hz);
                    m_lope_target    = m_noise.white() * tuning::idle_lope_rpm;
                }
                float lope_gate = std::clamp(1.0f - (m_rpm_smooth.z1 - tuning::idle_rpm) / 600.0f, 0.0f, 1.0f) * (1.0f - m_target_throttle);
                float lope      = m_lope_smooth.process(m_lope_target) * lope_gate;

                float rpm      = m_rpm_smooth.process(m_target_rpm + lope);
                float throttle = m_throttle_smooth.process(m_target_throttle);
                float load     = m_load_smooth.process(m_target_load);

                float rpm_norm = (rpm - tuning::idle_rpm) / (tuning::redline_rpm - tuning::idle_rpm);
                rpm_norm = std::clamp(rpm_norm, 0.0f, 1.0f);

                for (auto& cyl : m_cylinders)
                    cyl.set_rpm(rpm, m_sample_rate);

                // 0 at driven throttle, ramps to 1 on full lift-off at revs, feeds the afterfire rolls
                float overrun = (throttle < tuning::overrun_threshold && rpm_norm > 0.2f) ? (1.0f - throttle / tuning::overrun_threshold) : 0.0f;

                // combustion + per-bank pressure sums for v12 left/right exhausts
                float combustion_raw = 0.0f;
                float combustion_derivative = 0.0f;
                float bank_l = 0.0f;
                float bank_r = 0.0f;
                int firing_count = 0;

                for (size_t ci = 0; ci < m_cylinders.size(); ++ci)
                {
                    cylinder& cyl = m_cylinders[ci];
                    float pulse = cyl.tick(load, rpm_norm, overrun);
                    combustion_raw += pulse;
                    combustion_derivative += (pulse - cyl.prev_pressure);
                    if (cyl.bank_left)
                    {
                        bank_l += pulse;
                    }
                    else
                    {
                        bank_r += pulse;
                    }
                    if (cyl.is_firing)
                    {
                        firing_count++;
                    }
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

                // exhaust via convolution with real impulse response (engine-sim pipeline)
                // bleed each bank into the other so left/right always see signal even when only one bank is firing
                float bleed   = tuning::bank_cross_bleed;
                float bank_li = bank_l * (1.0f - bleed) + bank_r * bleed;
                float bank_ri = bank_r * (1.0f - bleed) + bank_l * bleed;

                float lf       = m_dc_block_l.process(bank_li);
                float lfp      = m_deriv_l.process(lf);
                float rn_l     = m_noise.pink();
                float rmixed_l = params.air_noise * rn_l + (1.0f - params.air_noise);
                float v_in_l   = params.df_f_mix * lfp + (1.0f - params.df_f_mix) * lf * rmixed_l;
                float wet_l    = m_ir_loaded ? m_conv_l.process(v_in_l) : v_in_l;
                float exhaust_l = wet_l * params.convolution_wet + v_in_l * (1.0f - params.convolution_wet);

                float rf       = m_dc_block_r.process(bank_ri);
                float rfp      = m_deriv_r.process(rf);
                float rn_r     = m_noise.pink();
                float rmixed_r = params.air_noise * rn_r + (1.0f - params.air_noise);
                float v_in_r   = params.df_f_mix * rfp + (1.0f - params.df_f_mix) * rf * rmixed_r;
                float wet_r    = m_ir_loaded ? m_conv_r.process(v_in_r) : v_in_r;
                float exhaust_r = wet_r * params.convolution_wet + v_in_r * (1.0f - params.convolution_wet);

                // notch out the ir's secondary 833 hz resonance that the firing harmonics keep blasting
                exhaust_l -= params.notch_depth * m_exhaust_notch_l.bandpass(exhaust_l);
                exhaust_r -= params.notch_depth * m_exhaust_notch_r.bandpass(exhaust_r);

                // mono exhaust used by the debug meter and the mono output path
                float exhaust = (exhaust_l + exhaust_r) * 0.5f;

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

                    // noise floor rides the load so the rattle does not read as hiss on the overrun
                    float chain_rattle = m_noise.white() * (0.1f + load * 0.2f + valve_tick * 0.7f);
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
                    {
                        m_turbo_spool += spool_diff * tuning::turbo_spool_up * dt;
                    }
                    else
                    {
                        m_turbo_spool += spool_diff * tuning::turbo_spool_down * dt;
                    }
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
                    {
                        m_wastegate_env = std::max(m_wastegate_env, fabsf(boost_delta) * 2.5f);
                    }

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
                        if (m_turbo_phase > 1.0f)
                        {
                            m_turbo_phase -= 1.0f;
                        }

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
                        if (m_turbo_flutter_phase > 1.0f)
                        {
                            m_turbo_flutter_phase -= 1.0f;
                        }

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

                // mono additive layers (combustion attack, induction, mechanical, turbo)
                float mono_layers = 0.0f;
                mono_layers += combustion  * params.combustion_level;
                mono_layers += induction   * tuning::induction_level;
                mono_layers += mechanical  * tuning::mechanical_level;
                mono_layers += turbo;
                mono_layers = m_dc_blocker.process(mono_layers);

                // per-channel mix, exhaust is stereo, layers are summed
                // optional gentle drive but no hard tanh (it was squaring the signal)
                float drive = 1.0f + throttle * 0.2f + rpm_norm * 0.1f + params.drive_extra;
                float left  = (exhaust_l * params.exhaust_level + mono_layers) * drive;
                float right = (exhaust_r * params.exhaust_level + mono_layers) * drive;

                // auto-leveler is the only loudness control, target is runtime-tweakable
                m_leveler.target = params.leveler_target;
                m_leveler.process(left, right);

                // final-stage dc blocker per channel kills any residual offset from unipolar cylinder pulse trains
                left  = m_dc_block_out_l.process(left);
                right = m_dc_block_out_r.process(right);

                // soft saturation, the knee hardens with throttle and rpm for extra roar under load
                // makeup pins the leveler target level to itself so only saturation density changes
                float drv    = tuning::output_soft_drive + throttle * tuning::output_drive_throttle + rpm_norm * tuning::output_drive_rpm;
                float makeup = m_leveler.target / tanhf(m_leveler.target * drv);
                left  = tanhf(left  * drv) * makeup;
                right = tanhf(right * drv) * makeup;

                float master = 0.7f + throttle * 0.2f + rpm_norm * 0.1f + params.master_offset;
                left  *= master;
                right *= master;

                float output = (left + right) * 0.5f;

                // debug accumulators
                combustion_sum += combustion * combustion;
                exhaust_sum    += exhaust * exhaust;
                induction_sum  += induction * induction;
                mechanical_sum += mechanical * mechanical;
                turbo_sum      += turbo * turbo;
                output_sum     += output * output;
                if (fabsf(output) > output_peak)
                {
                    output_peak = fabsf(output);
                }

                if ((i % 4) == 0)
                {
                    int wp = m_debug.waveform_write_pos;
                    m_debug.waveform[wp]     = output;
                    m_debug.waveform_cyl[wp] = bank_l;
                    m_debug.waveform_vin[wp] = v_in_l;
                    m_debug.waveform_exh[wp] = exhaust_l;
                    m_debug.waveform_write_pos = (wp + 1) % debug_data::waveform_size;
                }

                // full-rate ring for fft input
                m_spec_input[m_spec_pos] = output;
                m_spec_pos = (m_spec_pos + 1) % debug_data::fft_size;

                // wav dump capture
                if (m_dump_active && m_dump_progress < m_dump_total)
                {
                    m_dump_buffer[(size_t)m_dump_progress * 2]     = left;
                    m_dump_buffer[(size_t)m_dump_progress * 2 + 1] = right;
                    m_dump_progress++;
                    if (m_dump_progress >= m_dump_total)
                    {
                        m_dump_active = false;
                        m_debug.dump_ready = true;
                    }
                    m_debug.dump_progress = m_dump_progress;
                }

                // stereo output
                if (stereo)
                {
                    output_buffer[i * 2]     = left;
                    output_buffer[i * 2 + 1] = right;
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

            // expose leveler state
            m_debug.leveler_gain     = m_leveler.gain;
            m_debug.leveler_envelope = m_leveler.envelope;

            // fft of last fft_size output samples, hann window, log magnitude
            const int n = debug_data::fft_size;
            float re[debug_data::fft_size];
            float im[debug_data::fft_size];
            int read_pos = m_spec_pos;
            for (int k = 0; k < n; ++k)
            {
                int idx = (read_pos + k) % n;
                float w = 0.5f - 0.5f * cosf(TWO_PI * (float)k / (float)(n - 1));
                re[k] = m_spec_input[idx] * w;
                im[k] = 0.0f;
            }
            fft_radix2(re, im, n);
            const float norm = 2.0f / (float)n;
            for (int k = 0; k < debug_data::spectrum_bins; ++k)
            {
                float mag = sqrtf(re[k] * re[k] + im[k] * im[k]) * norm;
                float db  = 20.0f * log10f(mag + 1e-9f);
                m_debug.spectrum[k] = db;
            }
        }

        void reset()
        {
            for (auto& cyl : m_cylinders)
                cyl.reset();

            m_conv_l.reset();
            m_conv_r.reset();
            m_deriv_l.reset();
            m_deriv_r.reset();
            m_dc_block_l.reset();
            m_dc_block_r.reset();
            m_dc_block_out_l.reset();
            m_dc_block_out_r.reset();
            m_exhaust_notch_l.reset();
            m_exhaust_notch_r.reset();
            m_leveler.reset();

            std::fill(std::begin(m_spec_input), std::end(m_spec_input), 0.0f);
            m_spec_pos = 0;

            m_dump_active   = false;
            m_dump_progress = 0;
            m_dump_total    = 0;
            m_dump_buffer.clear();

            m_induction_res.reset();
            m_induction_body.reset();
            m_mechanical_hp.reset();
            m_mechanical_lp.reset();
            m_turbo_filter.reset();
            m_dc_blocker.reset();
            m_rpm_smooth.reset();
            m_throttle_smooth.reset();
            m_load_smooth.reset();
            m_lope_smooth.reset();
            m_lope_countdown = 0;
            m_lope_target    = 0.0f;

            m_turbo_spool = 0.0f;
            m_turbo_target_spool = 0.0f;
            m_turbo_phase = 0.0f;
            m_turbo_flutter_phase = 0.0f;
            m_turbo_flutter_env = 0.0f;
            m_wastegate_env = 0.0f;
            m_prev_throttle = 0.0f;
            m_prev_boost = 0.0f;

            m_turbo_whine_bp.reset();
            m_turbo_flutter_bp.reset();
            m_wastegate_bp.reset();
        }

        bool is_initialized() const { return m_initialized; }
        const debug_data& get_debug() const { return m_debug; }

        // begin a fresh dump capture, returns false if one is already running
        bool begin_dump(float seconds)
        {
            if (m_dump_active)
            {
                return false;
            }
            int frames = (int)(seconds * m_sample_rate);
            m_dump_buffer.assign((size_t)frames * 2, 0.0f);
            m_dump_total       = frames;
            m_dump_progress    = 0;
            m_dump_active      = true;
            m_debug.dump_total = frames;
            m_debug.dump_progress = 0;
            m_debug.dump_ready = false;
            return true;
        }

        bool dump_ready() const { return m_debug.dump_ready; }

        bool save_dump(const char* path)
        {
            if (!m_debug.dump_ready || m_dump_buffer.empty())
            {
                return false;
            }
            bool ok = write_wav_int16_stereo(path, m_dump_buffer.data(), m_dump_total, (int)m_sample_rate);
            m_debug.dump_ready = false;
            m_dump_buffer.clear();
            m_dump_total = 0;
            m_dump_progress = 0;
            m_debug.dump_total = 0;
            m_debug.dump_progress = 0;
            return ok;
        }

    private:
        bool  m_initialized = false;
        float m_sample_rate = tuning::sample_rate;

        float m_target_rpm      = tuning::idle_rpm;
        float m_target_throttle = 0.0f;
        float m_target_load     = 0.0f;
        float m_boost_pressure  = 0.0f;

        std::vector<cylinder> m_cylinders;

        // exhaust convolution path
        bool               m_ir_loaded = false;
        convolution_filter m_conv_l;
        convolution_filter m_conv_r;
        derivative_filter  m_deriv_l;
        derivative_filter  m_deriv_r;
        dc_blocker         m_dc_block_l;
        dc_blocker         m_dc_block_r;
        dc_blocker         m_dc_block_out_l;
        dc_blocker         m_dc_block_out_r;
        svf_filter         m_exhaust_notch_l;
        svf_filter         m_exhaust_notch_r;
        leveler            m_leveler;

        // full-rate ring buffer for fft input
        float m_spec_input[debug_data::fft_size] = {};
        int   m_spec_pos = 0;

        // wav dump capture buffer (interleaved stereo float)
        std::vector<float> m_dump_buffer;
        int  m_dump_total    = 0;
        int  m_dump_progress = 0;
        bool m_dump_active   = false;

        svf_filter m_induction_res, m_induction_body;
        svf_filter m_mechanical_hp, m_mechanical_lp;
        svf_filter m_turbo_filter;

        one_pole m_rpm_smooth;
        one_pole m_throttle_smooth;
        one_pole m_load_smooth;
        one_pole m_lope_smooth;

        int   m_lope_countdown = 0;
        float m_lope_target    = 0.0f;

        dc_blocker m_dc_blocker;
        noise_gen m_noise;

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
}
