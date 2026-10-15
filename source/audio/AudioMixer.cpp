/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========
#include "pch.h"
#include "AudioMixer.h"
#include <atomic>
#include <cmath>
//======================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        constexpr size_t group_count = static_cast<size_t>(AudioGroup::Count);

        // constant initialized, sources can query the mixer before any dynamic initializer has run
        atomic<float> gains[group_count]   = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        atomic<float> muffles[group_count] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        atomic<float> pitches[group_count] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        atomic<float> peaks[group_count]   = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        const char* names[group_count] = { "auto", "vehicle", "ambience", "sfx", "music" };

        size_t index(AudioGroup group)
        {
            return min(static_cast<size_t>(group), group_count - 1);
        }

        float sanitize(float value, float low, float high, float fallback)
        {
            return isfinite(value) ? clamp(value, low, high) : fallback;
        }
    }

    void AudioMixer::SetGain(AudioGroup group, float gain)
    {
        gains[index(group)].store(sanitize(gain, 0.0f, 4.0f, 1.0f), memory_order_relaxed);
    }

    float AudioMixer::GetGain(AudioGroup group)
    {
        return gains[index(group)].load(memory_order_relaxed);
    }

    void AudioMixer::SetMuffle(AudioGroup group, float amount)
    {
        muffles[index(group)].store(sanitize(amount, 0.0f, 1.0f, 0.0f), memory_order_relaxed);
    }

    float AudioMixer::GetMuffle(AudioGroup group)
    {
        return muffles[index(group)].load(memory_order_relaxed);
    }

    void AudioMixer::SetPitch(AudioGroup group, float pitch)
    {
        pitches[index(group)].store(sanitize(pitch, 0.1f, 4.0f, 1.0f), memory_order_relaxed);
    }

    float AudioMixer::GetPitch(AudioGroup group)
    {
        return pitches[index(group)].load(memory_order_relaxed);
    }

    void AudioMixer::Reset()
    {
        for (size_t i = 0; i < group_count; i++)
        {
            gains[i].store(1.0f, memory_order_relaxed);
            muffles[i].store(0.0f, memory_order_relaxed);
            pitches[i].store(1.0f, memory_order_relaxed);
            peaks[i].store(0.0f, memory_order_relaxed);
        }
    }

    void AudioMixer::ReportPeak(AudioGroup group, float peak)
    {
        atomic<float>& slot = peaks[index(group)];
        float current       = slot.load(memory_order_relaxed);
        while (peak > current && !slot.compare_exchange_weak(current, peak, memory_order_relaxed))
        {
        }
    }

    float AudioMixer::ConsumePeak(AudioGroup group)
    {
        return peaks[index(group)].exchange(0.0f, memory_order_relaxed);
    }

    float AudioMixer::GetMuffleCoefficient(float amount, float sample_rate)
    {
        // cutoff sweeps from 20 khz down to 350 hz, exponential so the middle of the range is musical
        const float cutoff = 20000.0f * powf(350.0f / 20000.0f, clamp(amount, 0.0f, 1.0f));
        return 1.0f - expf(-2.0f * 3.14159265f * cutoff / max(sample_rate, 8000.0f));
    }

    const char* AudioMixer::GetName(AudioGroup group)
    {
        return names[index(group)];
    }

    bool AudioMixer::FromName(const string& name, AudioGroup& group)
    {
        for (size_t i = 0; i < group_count; i++)
        {
            if (name == names[i])
            {
                group = static_cast<AudioGroup>(i);
                return true;
            }
        }
        if (name == "engine" || name == "car" || name == "cars")
        {
            group = AudioGroup::Vehicle;
            return true;
        }
        if (name == "world" || name == "ambient")
        {
            group = AudioGroup::Ambience;
            return true;
        }
        return false;
    }
}
