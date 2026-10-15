/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====
#include <cstdint>
#include <string>
//================

namespace spartan
{
    // every audio source belongs to one group, the mixer scales each group as a whole so a director
    // can push the engine up and the music down without touching individual sources
    enum class AudioGroup : uint8_t
    {
        Auto,     // resolved by the source: ambient beds are ambience, everything else is sfx
        Vehicle,
        Ambience,
        Sfx,
        Music,
        Count
    };

    class AudioMixer
    {
    public:
        // gain multiplies the group, 1 is untouched
        static void SetGain(AudioGroup group, float gain);
        static float GetGain(AudioGroup group);

        // 0 is clear, 1 is a heavy low pass, the muffled world of a slow motion shot
        static void SetMuffle(AudioGroup group, float amount);
        static float GetMuffle(AudioGroup group);

        // playback rate multiplier, below 1 drops the pitch with the tempo
        static void SetPitch(AudioGroup group, float pitch);
        static float GetPitch(AudioGroup group);

        // every group back to unity
        static void Reset();

        // sources report the loudest sample they produced, from any thread
        static void ReportPeak(AudioGroup group, float peak);
        // the loudest sample since the previous call, post group gain
        static float ConsumePeak(AudioGroup group);

        // one pole low pass coefficient for a muffle amount at a sample rate
        static float GetMuffleCoefficient(float amount, float sample_rate);

        static const char* GetName(AudioGroup group);
        static bool FromName(const std::string& name, AudioGroup& group);
    };
}
