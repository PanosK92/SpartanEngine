/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==========
#include "IComponent.h"
#include <memory>
#include <string>
//=====================

namespace Spartan
{
    class AudioClip;

    class SPARTAN_CLASS AudioSource : public IComponent
    {
    public:
        AudioSource(Context* context, Entity* entity, uint32_t id = 0);
        ~AudioSource() = default;

        //= INTERFACE ================================
        void OnInitialize() override;
        void OnStart() override;
        void OnStop() override;
        void OnRemove() override;
        void OnTick(float delta_time) override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;
        //============================================

        //= PROPERTIES ===================================================================
        void SetAudioClip(const std::string& file_path);
        std::string GetAudioClipName() const;

        bool Play() const;
        bool Stop() const;

        bool GetMute() const { return m_mute; }
        void SetMute(bool mute);

        bool GetPlayOnStart() const                        { return m_play_on_start; }
        void SetPlayOnStart(const bool play_on_start)    { m_play_on_start = play_on_start; }

        bool GetLoop() const            { return m_loop; }
        void SetLoop(const bool loop)    { m_loop = loop; }

        int GetPriority() const { return m_priority; }
        void SetPriority(int priority);

        float GetVolume() const { return m_volume; }
        void SetVolume(float volume);

        float GetPitch() const { return m_pitch; }
        void SetPitch(float pitch);

        float GetPan() const { return m_pan; }
        void SetPan(float pan);
        //================================================================================

    private:
        std::shared_ptr<AudioClip> m_audio_clip;
        bool m_mute;
        bool m_play_on_start;
        bool m_loop;
        int m_priority;
        float m_volume;
        float m_pitch;
        float m_pan;
        bool m_audio_clip_loaded;
    };
}
