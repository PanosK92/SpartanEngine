/*
copyright(c) 2016-2025 panos karabelas

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

namespace spartan
{
    class AudioSource : public Component
    {
    public:
        AudioSource(Entity* entity);
        ~AudioSource();

        // component interface
        void OnInitialize() override;
        void OnStart() override;
        void OnStop() override;
        void OnRemove() override;
        void OnTick() override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;

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
        void SetPitch(float pitch);

        float GetPan() const { return m_pan; }
        void SetPan(float pan);

    private:
        std::string m_name        = "N/A";
        bool    m_is_3d           = false;
        bool    m_mute            = false;
        bool    m_loop            = true;
        bool    m_play_on_start   = true;
        float   m_volume          = 1.0f;
        float   m_attenuation    = 1.0f;
        float   m_pitch           = 1.0f;
        float   m_pan             = 0.0f;
        bool m_is_playing         = false;
        uint8_t* m_buffer         = nullptr;
        uint32_t m_length         = 0;
        SDL_AudioStream* m_stream = nullptr;
    };
}
