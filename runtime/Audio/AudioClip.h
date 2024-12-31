/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES =============================
#include "../Resource/IResource.h"
#include "../Math/Vector3.h"
//========================================

namespace Spartan
{
    enum class PlayMode
    {
        Memory,
        Stream
    };

    class AudioClip : public IResource
    {
    public:
        AudioClip();
        ~AudioClip();

        // iresource
        void LoadFromFile(const std::string& file_path) override;
        void SaveToFile(const std::string& file_path) override;

        void Play(const bool loop, const bool is_3d);
        void Pause();
        void Stop();

        // Looping
        bool GetLoop() const;
        void SetLoop(bool loop);

        // Set's the volume [0.0f, 1.0f]
        bool SetVolume(float volume);

        // Sets the mute state effectively silencing it or returning it to its normal volume.
        bool SetMute(bool mute);

        // Set's the priority for the channel [0, 255]
        bool SetPriority(int priority);

        // Sets the pitch value
        bool SetPitch(float pitch);

        // Sets the pan level
        bool SetPan(float pan);

        // Makes the audio use the 3D attributes of the transform
        void SetTransform(Entity* entity) { m_entity = entity; }

        // 3D audio
        void Set3d(const bool enabled);
        bool Get3d() const;

        // Should be called per frame to update the 3D attributes of the sound
        bool Update();

        bool IsPlaying();
        bool IsPaused();
        float GetProgress();

        void ResetChannel() { m_fmod_channel = nullptr; }

    private:
        //= CREATION ===================================
        bool CreateSound(const std::string& file_path);
        bool CreateStream(const std::string& file_path);
        //==============================================
        int GetSoundMode() const;

        Entity* m_entity     = nullptr;
        void* m_fmod_sound   = nullptr;
        void* m_fmod_channel = nullptr;
        PlayMode m_playMode  = PlayMode::Memory;
        float m_distance_min = 0.1f;
        float m_distance_max = 1000.0f;
    };
}
