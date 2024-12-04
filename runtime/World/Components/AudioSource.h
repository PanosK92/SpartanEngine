/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES =========
#include "Component.h"
#include <memory>
#include <string>
//====================

namespace Spartan
{
    class AudioClip;

    class AudioSource : public Component
    {
    public:
        AudioSource(Entity* entity);
        ~AudioSource();

        // IComponent
        void OnInitialize() override;
        void OnStart() override;
        void OnStop() override;
        void OnRemove() override;
        void OnTick() override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;

        //= PROPERTIES ===================================================================
        void SetAudioClip(const std::string& file_path);
        std::string GetAudioClipName() const;

        bool IsPlaying() const;
        void Play() const;
        void Stop() const;
        float GetProgress() const;

        bool GetMute() const { return m_mute; }
        void SetMute(bool mute);

        bool GetPlayOnStart() const                   { return m_play_on_start; }
        void SetPlayOnStart(const bool play_on_start) { m_play_on_start = play_on_start; }

        bool GetLoop() const { return m_loop; }
        void SetLoop(const bool loop);

        int GetPriority() const { return m_priority; }
        void SetPriority(int priority);

        float GetVolume() const { return m_volume; }
        void SetVolume(float volume);

        float GetPitch() const { return m_pitch; }
        void SetPitch(float pitch);

        float GetPan() const { return m_pan; }
        void SetPan(float pan);

        bool Get3d() const { return m_3d; }
        void Set3d(const bool enabled);
        //================================================================================

    private:
        bool m_mute              = false;
        bool m_loop              = true;
        bool m_3d                = false;
        bool m_audio_clip_loaded = false;
        bool m_play_on_start     = true;
        int m_priority           = 128;
        float m_volume           = 1.0f;
        float m_pitch            = 1.0f;
        float m_pan              = 0.0f;
        std::shared_ptr<AudioClip> m_audio_clip;
    };
}
