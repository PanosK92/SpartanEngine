/*
Copyright(c) 2015-2025 Panos Karabelas

permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "software"), to deal
in the software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the software, and to permit persons to whom the software is furnished
to do so, subject to the following conditions :

the above copyright notice and this permission notice shall be included in
all copies or substantial portions of the software.

the software is provided "as is", without warranty of any kind, express or
implied, including but not limited to the warranties of merchantability, fitness
for a particular purpose and noninfringement. in no event shall the authors or
copyright holders be liable for any claim, damages or other liability, whether
in an action of contract, tort or otherwise, arising from, out of or in
connection with the software or the use or other dealings in the software.
*/

#pragma once

//= includes =========
#include "Component.h"
#include <string>
//=====================

struct SDL_AudioStream;
struct SDL_AudioSpec;
namespace audio_clip_cache
{
    struct AudioClip;
}

namespace spartan
{
    class AudioSource : public Component
    {
    public:
        AudioSource(Entity* entity);
        ~AudioSource();

        // component interface
        void Initialize() override;
        void Start() override;
        void Stop() override;
        void Remove() override;
        void OnTick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        void SetAudioClip(const std::string& file_path);
        const std::string& GetAudioClipName() const { return m_name; };

        bool IsPlaying() { return m_is_playing; }
        void Play();
        void Stop();
        float GetProgress() const;

        bool GetMute() const { return m_mute; }
        void SetMute(bool mute);

        bool GetPlayOnStart() const                   { return m_play_on_start; }
        void SetPlayOnStart(const bool play_on_start) { m_play_on_start = play_on_start; }

        bool GetLoop() const          { return m_loop; }
        void SetLoop(const bool loop) { m_loop = loop; }

        bool GetIs3d() const           { return m_is_3d; }
        void SetIs3d(const bool is_3d) { m_is_3d = is_3d; }

        float GetVolume() const { return m_volume; }
        void SetVolume(float volume);

        float GetPitch() const { return m_pitch; }
        void SetPitch(const float pitch);

    private:
        void FeedAudioChunk();

        std::string m_name                             = "N/A";
        bool m_is_3d                                   = false;
        bool m_mute                                    = false;
        bool m_loop                                    = true;
        bool m_play_on_start                           = true;
        float m_volume                                 = 1.0f;
        float m_pitch                                  = 1.0f;
        float m_attenuation                            = 1.0f;
        float m_pan                                    = 0.0f; // -1.0 (left) to 1.0 (right)
        bool m_is_playing                              = false;
        uint32_t m_position                            = 0; // in bytes
        SDL_AudioStream* m_stream                      = nullptr;
        float m_doppler_ratio                          = 1.0f;
        math::Vector3 position_previous                = math::Vector3::Zero;
        std::shared_ptr<audio_clip_cache::AudioClip> m_clip = nullptr;
        std::string m_file_path;
    };
}
