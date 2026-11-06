/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ================
#include "pch.h"
#include "SequencerMusic.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <SDL3/SDL_audio.h>
#include <SDL3/dr_mp3.h>
//===========================

//= NAMESPACES =====
using namespace std;
//==================

namespace sequencer_music
{
    namespace
    {
        constexpr float envelope_rate    = 100.0f; // envelope frames per second
        constexpr float bpm_min          = 60.0f;
        constexpr float bpm_max          = 200.0f;
        constexpr float bpm_prior        = 120.0f; // most music people cut to sits within an octave of it
        constexpr float novelty_min      = 0.35f;  // log energy ratio across a bar line that starts a new section
        constexpr size_t section_bars_min = 4;

        struct Envelope
        {
            float rate = 0.0f;        // frames per second
            vector<float> onset;      // how sharply the band energies rise, above their local average
            vector<float> energy;     // full band mean square per frame
            vector<float> energy_low; // below ~150 hz, kicks and bass
        };

        bool decode_mono(const string& path, vector<float>& samples, uint32_t& sample_rate, string& error)
        {
            string extension = filesystem::path(path).extension().string();
            transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });

            if (extension == ".mp3")
            {
                drmp3_config config      = {};
                drmp3_uint64 frame_count = 0;
                float* pcm               = drmp3_open_file_and_read_pcm_frames_f32(path.c_str(), &config, &frame_count, nullptr);
                if (!pcm)
                {
                    error = "could not decode '" + path + "'";
                    return false;
                }
                if (config.channels == 0 || config.sampleRate == 0)
                {
                    drmp3_free(pcm, nullptr);
                    error = "could not decode '" + path + "'";
                    return false;
                }

                const uint32_t channels = config.channels;
                samples.resize(static_cast<size_t>(frame_count));
                for (size_t frame = 0; frame < samples.size(); frame++)
                {
                    float sum = 0.0f;
                    for (uint32_t c = 0; c < channels; c++)
                    {
                        sum += pcm[frame * channels + c];
                    }
                    samples[frame] = sum / static_cast<float>(channels);
                }
                sample_rate = config.sampleRate;
                drmp3_free(pcm, nullptr);
                return true;
            }

            if (extension == ".wav")
            {
                SDL_AudioSpec spec = {};
                Uint8* buffer      = nullptr;
                Uint32 length      = 0;
                if (!SDL_LoadWAV(path.c_str(), &spec, &buffer, &length))
                {
                    error = "could not decode '" + path + "': " + SDL_GetError();
                    return false;
                }

                const SDL_AudioSpec target = { SDL_AUDIO_F32, 1, spec.freq };
                Uint8* converted           = nullptr;
                int converted_length       = 0;
                const bool ok              = SDL_ConvertAudioSamples(&spec, buffer, static_cast<int>(length), &target, &converted, &converted_length);
                SDL_free(buffer);
                if (!ok || spec.freq <= 0)
                {
                    SDL_free(converted);
                    error = "could not convert '" + path + "': " + SDL_GetError();
                    return false;
                }

                const float* begin = reinterpret_cast<const float*>(converted);
                samples.assign(begin, begin + static_cast<size_t>(converted_length) / sizeof(float));
                SDL_free(converted);
                sample_rate = static_cast<uint32_t>(spec.freq);
                return true;
            }

            error = "only mp3 and wav files can be analyzed";
            return false;
        }

        Envelope build_envelope(const vector<float>& samples, uint32_t sample_rate)
        {
            Envelope envelope;
            const uint32_t hop  = max(1u, static_cast<uint32_t>(static_cast<float>(sample_rate) / envelope_rate));
            const size_t frames = samples.size() / hop;
            envelope.rate       = static_cast<float>(sample_rate) / static_cast<float>(hop);

            // two cascaded one pole low passes per split are enough to pull the kick, the body and the air apart
            const float pi    = 3.14159265f;
            const float a_low = 1.0f - expf(-2.0f * pi * 150.0f / static_cast<float>(sample_rate));
            const float a_mid = 1.0f - expf(-2.0f * pi * 2000.0f / static_cast<float>(sample_rate));
            float low_1       = 0.0f;
            float low_2       = 0.0f;
            float mid_1       = 0.0f;
            float mid_2       = 0.0f;

            vector<float> bands[3];
            for (vector<float>& band : bands)
            {
                band.assign(frames, 0.0f);
            }
            envelope.energy.assign(frames, 0.0f);
            for (size_t frame = 0; frame < frames; frame++)
            {
                float sums[3] = {};
                float full    = 0.0f;
                for (uint32_t i = 0; i < hop; i++)
                {
                    const float x = samples[frame * hop + i];
                    low_1        += a_low * (x - low_1);
                    low_2        += a_low * (low_1 - low_2);
                    mid_1        += a_mid * (x - mid_1);
                    mid_2        += a_mid * (mid_1 - mid_2);
                    const float low  = low_2;
                    const float mid  = mid_2 - low_2;
                    const float high = x - mid_2;
                    sums[0]         += low * low;
                    sums[1]         += mid * mid;
                    sums[2]         += high * high;
                    full            += x * x;
                }
                for (size_t b = 0; b < 3; b++)
                {
                    bands[b][frame] = sums[b] / static_cast<float>(hop);
                }
                envelope.energy[frame] = full / static_cast<float>(hop);
            }

            // rises in log energy, so a quiet hi hat counts as much as a loud kick
            vector<float> rise(frames, 0.0f);
            for (const vector<float>& band : bands)
            {
                double mean = 0.0;
                for (float value : band)
                {
                    mean += value;
                }
                mean                   /= static_cast<double>(max<size_t>(frames, 1));
                const float floor_level = static_cast<float>(mean) * 1e-3f + 1e-12f;
                float previous          = frames > 0 ? logf(band[0] + floor_level) : 0.0f;
                for (size_t frame = 1; frame < frames; frame++)
                {
                    const float level = logf(band[frame] + floor_level);
                    rise[frame]      += max(level - previous, 0.0f);
                    previous          = level;
                }
            }
            envelope.energy_low = move(bands[0]);

            // only rises above the local average are onsets, the swell of a sustained note is not
            const size_t window = static_cast<size_t>(envelope.rate * 0.25f);
            vector<double> prefix(frames + 1, 0.0);
            for (size_t frame = 0; frame < frames; frame++)
            {
                prefix[frame + 1] = prefix[frame] + rise[frame];
            }
            envelope.onset.assign(frames, 0.0f);
            for (size_t frame = 0; frame < frames; frame++)
            {
                const size_t begin = frame > window ? frame - window : 0;
                const size_t end   = min(frames, frame + window + 1);
                const float mean   = static_cast<float>((prefix[end] - prefix[begin]) / static_cast<double>(end - begin));
                envelope.onset[frame] = max(rise[frame] - mean, 0.0f);
            }

            return envelope;
        }

        float sample_at(const vector<float>& values, float position)
        {
            const size_t index = static_cast<size_t>(position);
            if (index + 1 >= values.size())
            {
                return 0.0f;
            }
            const float t = position - static_cast<float>(index);
            return values[index] + (values[index + 1] - values[index]) * t;
        }

        // the average onset on a grid of the given period, at its best phase, a slightly wrong tempo drifts off the beats over a whole song
        float comb_score(const vector<float>& onset, float period, float& best_phase, float& mean_score)
        {
            const float length = static_cast<float>(onset.size());
            float best         = 0.0f;
            double sum         = 0.0;
            uint32_t phases    = 0;
            best_phase         = 0.0f;
            for (float phase = 0.0f; phase < period; phase += 0.5f)
            {
                double total   = 0.0;
                uint32_t beats = 0;
                for (float position = phase; position < length - 1.0f; position = phase + period * static_cast<float>(++beats))
                {
                    total += sample_at(onset, position);
                }
                const float score = beats > 0 ? static_cast<float>(total / beats) : 0.0f;
                sum              += score;
                phases++;
                if (score > best)
                {
                    best       = score;
                    best_phase = phase;
                }
            }
            mean_score = phases > 0 ? static_cast<float>(sum / phases) : 0.0f;
            return best;
        }

        // autocorrelation of the onsets, leaning toward tempos near the prior so a half or double time reading loses
        float estimate_tempo(const vector<float>& onset, float rate)
        {
            const size_t count   = onset.size();
            const size_t lag_min = static_cast<size_t>(floorf(rate * 60.0f / bpm_max));
            const size_t lag_max = static_cast<size_t>(ceilf(rate * 60.0f / bpm_min));
            float best           = -1.0f;
            float best_bpm       = bpm_prior;
            for (size_t lag = max<size_t>(lag_min, 1); lag <= lag_max && lag < count; lag++)
            {
                double sum = 0.0;
                for (size_t i = 0; i + lag < count; i++)
                {
                    sum += static_cast<double>(onset[i]) * onset[i + lag];
                }
                const float correlation = static_cast<float>(sum / static_cast<double>(count - lag));
                const float bpm         = rate * 60.0f / static_cast<float>(lag);
                const float octaves     = log2f(bpm / bpm_prior);
                const float weighted    = correlation * expf(-0.5f * octaves * octaves);
                if (weighted > best)
                {
                    best     = weighted;
                    best_bpm = bpm;
                }
            }
            return best_bpm;
        }

        // sections turn over on bar lines, so the beat of the bar where the energy jumps most often is the downbeat
        uint32_t find_downbeat(const Envelope& envelope, float first_beat, float period, uint32_t beats_per_bar)
        {
            double mean_low  = 0.0;
            double mean_full = 0.0;
            for (size_t i = 0; i < envelope.energy.size(); i++)
            {
                mean_low  += envelope.energy_low[i];
                mean_full += envelope.energy[i];
            }
            const double frames      = static_cast<double>(max<size_t>(envelope.energy.size(), 1));
            const float floor_low    = static_cast<float>(mean_low / frames) * 1e-3f + 1e-12f;
            const float floor_full   = static_cast<float>(mean_full / frames) * 1e-3f + 1e-12f;
            const float length       = static_cast<float>(envelope.energy.size());

            vector<float> low;
            vector<float> full;
            for (uint32_t beat = 0; first_beat + period * static_cast<float>(beat + 1) <= length; beat++)
            {
                const size_t begin = static_cast<size_t>(first_beat + period * static_cast<float>(beat));
                const size_t end   = static_cast<size_t>(first_beat + period * static_cast<float>(beat + 1));
                double sum_low     = 0.0;
                double sum_full    = 0.0;
                for (size_t i = begin; i < end; i++)
                {
                    sum_low  += envelope.energy_low[i];
                    sum_full += envelope.energy[i];
                }
                const double span = static_cast<double>(max<size_t>(end - begin, 1));
                low.push_back(logf(static_cast<float>(sum_low / span) + floor_low));
                full.push_back(logf(static_cast<float>(sum_full / span) + floor_full));
            }

            vector<float> scores(beats_per_bar, 0.0f);
            for (size_t i = 1; i < low.size(); i++)
            {
                scores[i % beats_per_bar] += fabsf(low[i] - low[i - 1]) + fabsf(full[i] - full[i - 1]);
            }
            return static_cast<uint32_t>(max_element(scores.begin(), scores.end()) - scores.begin());
        }

        void find_sections(Analysis& analysis)
        {
            const vector<float>& energy = analysis.bar_energy;
            const size_t bars           = energy.size();
            const float bar_length      = analysis.beat_length * static_cast<float>(analysis.beats_per_bar);
            if (bars == 0)
            {
                analysis.sections.push_back({ 0.0f, analysis.duration, 1.0f, SectionKind::Intro });
                return;
            }

            auto mean = [&](size_t begin, size_t end)
            {
                float sum = 0.0f;
                for (size_t i = begin; i < end; i++)
                {
                    sum += energy[i];
                }
                return end > begin ? sum / static_cast<float>(end - begin) : 0.0f;
            };

            // how much louder or quieter the two bars after each bar line are than the two before it
            vector<float> novelty(bars + 1, 0.0f);
            for (size_t k = 1; k < bars; k++)
            {
                const float before = mean(k >= 2 ? k - 2 : 0, k);
                const float after  = mean(k, min(bars, k + 2));
                novelty[k]         = fabsf(logf((after + 0.05f) / (before + 0.05f)));
            }

            vector<size_t> candidates;
            for (size_t k = 2; k + 2 <= bars; k++)
            {
                if (novelty[k] > novelty_min && novelty[k] >= novelty[k - 1] && novelty[k] >= novelty[k + 1])
                {
                    candidates.push_back(k);
                }
            }
            sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) { return novelty[a] > novelty[b]; });

            // the strongest changes win, weaker ones too close to them are the same change smeared over a few bars
            vector<size_t> boundaries;
            for (size_t k : candidates)
            {
                const bool clear = all_of(boundaries.begin(), boundaries.end(), [&](size_t other) { return (k > other ? k - other : other - k) >= section_bars_min; });
                if (clear)
                {
                    boundaries.push_back(k);
                }
            }
            sort(boundaries.begin(), boundaries.end());
            boundaries.insert(boundaries.begin(), 0);
            boundaries.push_back(bars);

            float loudest = 0.0f;
            for (size_t i = 0; i + 1 < boundaries.size(); i++)
            {
                Section section;
                section.start  = i == 0 ? 0.0f : analysis.first_downbeat + bar_length * static_cast<float>(boundaries[i]);
                section.end    = i + 2 == boundaries.size() ? analysis.duration : analysis.first_downbeat + bar_length * static_cast<float>(boundaries[i + 1]);
                section.energy = mean(boundaries[i], boundaries[i + 1]);
                loudest        = max(loudest, section.energy);
                analysis.sections.push_back(section);
            }

            vector<Section>& sections = analysis.sections;
            for (Section& section : sections)
            {
                section.energy = loudest > 0.0f ? section.energy / loudest : 0.0f;
            }
            for (size_t i = 0; i < sections.size(); i++)
            {
                const bool high      = sections[i].energy >= 0.75f;
                const bool next_high = i + 1 < sections.size() && sections[i + 1].energy >= 0.75f;
                if (high)
                {
                    sections[i].kind = SectionKind::Drop;
                }
                else if (i == 0)
                {
                    sections[i].kind = SectionKind::Intro;
                }
                else if (next_high)
                {
                    sections[i].kind = SectionKind::Build;
                }
                else if (i + 1 == sections.size())
                {
                    sections[i].kind = SectionKind::Outro;
                }
                else
                {
                    sections[i].kind = SectionKind::Breakdown;
                }
            }
        }

        void run(Analysis& analysis)
        {
            vector<float> samples;
            uint32_t sample_rate = 0;
            if (!decode_mono(analysis.path, samples, sample_rate, analysis.error))
            {
                return;
            }
            analysis.duration = static_cast<float>(samples.size()) / static_cast<float>(sample_rate);
            if (analysis.duration < 4.0f)
            {
                analysis.error = "the file is too short to find a tempo";
                return;
            }

            const Envelope envelope = build_envelope(samples, sample_rate);
            samples.clear();
            samples.shrink_to_fit();
            const float rate = envelope.rate;

            // the autocorrelation lands within a lag of the tempo, the comb then locks it over the whole song
            float bpm        = estimate_tempo(envelope.onset, rate);
            float best       = -1.0f;
            float best_phase = 0.0f;
            float best_mean  = 0.0f;
            auto search = [&](float center, float span, float step)
            {
                float found = center;
                for (float candidate = center - span; candidate <= center + span; candidate += step)
                {
                    float phase       = 0.0f;
                    float mean_score  = 0.0f;
                    const float score = comb_score(envelope.onset, rate * 60.0f / candidate, phase, mean_score);
                    if (score > best)
                    {
                        best       = score;
                        best_phase = phase;
                        best_mean  = mean_score;
                        found      = candidate;
                    }
                }
                return found;
            };
            bpm = search(bpm, bpm * 0.03f, 0.05f);
            bpm = search(bpm, 0.06f, 0.002f);

            analysis.bpm         = roundf(bpm * 100.0f) / 100.0f;
            analysis.beat_length = 60.0f / analysis.bpm;
            analysis.confidence  = best > 0.0f ? clamp((best - best_mean) / best, 0.0f, 1.0f) : 0.0f;

            const float period     = rate * 60.0f / bpm;
            const uint32_t beat    = find_downbeat(envelope, best_phase, period, analysis.beats_per_bar);
            const float bar_length = analysis.beat_length * static_cast<float>(analysis.beats_per_bar);
            const float downbeat   = (best_phase + period * static_cast<float>(beat)) / rate;
            analysis.first_downbeat = downbeat - floorf(downbeat / bar_length) * bar_length;

            // loudness and weight of every bar, against the loud end of the song rather than its single loudest bar
            vector<float> full;
            vector<float> low;
            for (float start = analysis.first_downbeat; start + bar_length <= analysis.duration + 0.001f; start += bar_length)
            {
                const size_t begin = min(static_cast<size_t>(start * rate), envelope.energy.size());
                const size_t end   = min(static_cast<size_t>((start + bar_length) * rate), envelope.energy.size());
                double sum_full    = 0.0;
                double sum_low     = 0.0;
                for (size_t i = begin; i < end; i++)
                {
                    sum_full += envelope.energy[i];
                    sum_low  += envelope.energy_low[i];
                }
                const double span = static_cast<double>(max<size_t>(end - begin, 1));
                full.push_back(sqrtf(static_cast<float>(sum_full / span)));
                low.push_back(sqrtf(static_cast<float>(sum_low / span)));
            }
            auto loud = [](vector<float> values)
            {
                if (values.empty())
                {
                    return 1.0f;
                }
                const size_t index = static_cast<size_t>(static_cast<float>(values.size() - 1) * 0.95f);
                nth_element(values.begin(), values.begin() + index, values.end());
                return max(values[index], 1e-6f);
            };
            const float loud_full = loud(full);
            const float loud_low  = loud(low);
            for (size_t i = 0; i < full.size(); i++)
            {
                analysis.bar_energy.push_back(min(0.5f * full[i] / loud_full + 0.5f * low[i] / loud_low, 1.0f));
            }

            find_sections(analysis);
        }
    }

    const char* get_section_name(SectionKind kind)
    {
        switch (kind)
        {
            case SectionKind::Intro:     return "intro";
            case SectionKind::Build:     return "build";
            case SectionKind::Drop:      return "drop";
            case SectionKind::Breakdown: return "breakdown";
            case SectionKind::Outro:     return "outro";
        }
        return "section";
    }

    const Analysis& analyze(const string& path)
    {
        struct Entry
        {
            filesystem::file_time_type stamp;
            Analysis analysis;
        };
        static unordered_map<string, Entry> cache;

        error_code error;
        const filesystem::file_time_type stamp = path.empty() ? filesystem::file_time_type() : filesystem::last_write_time(path, error);
        auto it = cache.find(path);
        if (it != cache.end() && !path.empty() && !error && it->second.stamp == stamp)
        {
            return it->second.analysis;
        }

        Entry& entry   = cache[path];
        entry.stamp    = stamp;
        entry.analysis = Analysis();
        entry.analysis.path = path;
        if (path.empty())
        {
            entry.analysis.error = "there is no music file to analyze";
        }
        else if (error)
        {
            entry.analysis.error = "no audio file at '" + path + "'";
        }
        else
        {
            run(entry.analysis);
        }
        return entry.analysis;
    }
}
