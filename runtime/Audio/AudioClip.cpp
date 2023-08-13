/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =============================
#include "pch.h"
#include "AudioClip.h"
#include <fmod.hpp>
#include "Audio.h"
#include "../World/Components/Transform.h"
#include "../IO/FileStream.h"
//========================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    AudioClip::AudioClip() : IResource(ResourceType::Audio)
    {

    }

    AudioClip::~AudioClip()
    {
        if (FMOD::Sound* sound = static_cast<FMOD::Sound*>(m_fmod_sound))
        {
            Audio::HandleErrorFmod(sound->release());
        }
    }

    bool AudioClip::LoadFromFile(const string& file_path)
    {
        // Native
        if (FileSystem::GetExtensionFromFilePath(file_path) == EXTENSION_AUDIO)
        {
            auto file = make_unique<FileStream>(file_path, FileStream_Read);
            if (!file->IsOpen())
                return false;

            SetResourceFilePath(file->ReadAs<string>());

            file->Close();
        }
        // Foreign
        else
        {
            SetResourceFilePath(file_path);
        }

        return (m_playMode == PlayMode::Memory) ? CreateSound(GetResourceFilePath()) : CreateStream(GetResourceFilePath());
    }

    bool AudioClip::SaveToFile(const string& file_path)
    {
        auto file = make_unique<FileStream>(file_path, FileStream_Write);
        if (!file->IsOpen())
            return false;

        file->Write(GetResourceFilePath());

        file->Close();

        return true;
    }

    void AudioClip::Play(const bool loop, const bool is_3d)
    {
        if (IsPlaying())
            return;

        Audio::PlaySound(m_fmod_sound, m_fmod_channel);

        SetLoop(loop);
        Set3d(is_3d);
    }

    void AudioClip::Pause()
    {
        if (!IsPaused())
            return;

        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setPaused(true));
        }
    }

    void AudioClip::Stop()
    {
        if (!IsPlaying())
            return;

        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->stop());
        }
    }

    bool AudioClip::GetLoop() const
    {
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            FMOD_MODE current_mode;
            channel->getMode(&current_mode);

            return (current_mode & FMOD_LOOP_NORMAL) != 0;
        }

        return false;
    }

    void AudioClip::SetLoop(const bool loop)
    {
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            FMOD_MODE current_mode;
            channel->getMode(&current_mode);

            if (loop)
            {
                current_mode |= FMOD_LOOP_NORMAL;
            }
            else
            {
                current_mode &= ~FMOD_LOOP_NORMAL;
            }

            if (channel->setMode(current_mode) != FMOD_OK)
            {
                SP_LOG_ERROR("Failed");
            }
        }
    }

    bool AudioClip::SetVolume(float volume)
    {
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setVolume(volume));
        }

        return false;
    }

    bool AudioClip::SetMute(const bool mute)
    {
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setMute(mute));
        }

        return false;
    }

    bool AudioClip::SetPriority(const int priority)
    {
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setPriority(priority));
        }

        return false;
    }

    bool AudioClip::SetPitch(const float pitch)
    {
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setPitch(pitch));
        }

        return false;
    }

    bool AudioClip::SetPan(const float pan)
    {
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(channel->setPan(pan));
        }

        return false;
    }

    void AudioClip::Set3d(const bool enabled)
    {
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            channel->setMode(enabled ? FMOD_3D : FMOD_2D);
        }
    }

    bool AudioClip::Get3d() const
    {
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            FMOD_MODE current_mode;
            channel->getMode(&current_mode);

            // returns true if FMOD_3D is set, false otherwise
            return (current_mode & FMOD_3D) != 0; 
        }

        // Default or error value
        return false;
    }

    bool AudioClip::Update()
    {
        if (!m_transform)
            return true;

        const Vector3 pos = m_transform->GetPosition();

        FMOD_VECTOR f_mod_pos = { pos.x, pos.y, pos.z };
        FMOD_VECTOR f_mod_vel = { 0, 0, 0 };

        return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->set3DAttributes(&f_mod_pos, &f_mod_vel));
    }

    bool AudioClip::IsPlaying()
    {
        bool is_playing = false;

        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            Audio::HandleErrorFmod(channel->isPlaying(&is_playing));
        }

        return is_playing;
    }

    bool AudioClip::IsPaused()
    {
        bool is_paused = false;

        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            Audio::HandleErrorFmod(channel->getPaused(&is_paused));
        }

        return is_paused;
    }

    bool AudioClip::CreateSound(const string& file_path)
    {
        // Create sound
        if (!Audio::CreateSound(file_path, GetSoundMode(), m_fmod_sound))
            return false;

        // Set 3D min max distance
        if (!Audio::HandleErrorFmod(static_cast<FMOD::Sound*>(m_fmod_sound)->set3DMinMaxDistance(m_distance_min, m_distance_max)))
            return false;

        return true;
    }

    bool AudioClip::CreateStream(const string& file_path)
    {
        // Create sound
        if (!Audio::CreateStream(file_path, GetSoundMode(), m_fmod_sound))
            return false;

        // Set 3D min max distance
        if (!Audio::HandleErrorFmod(static_cast<FMOD::Sound*>(m_fmod_sound)->set3DMinMaxDistance(m_distance_min, m_distance_max)))
            return false;

        return true;
    }

    int AudioClip::GetSoundMode() const
    {
        unsigned int sound_mode  = 0;
        sound_mode              |= FMOD_2D;
        sound_mode              |= FMOD_3D_LINEARROLLOFF;
        sound_mode              |= FMOD_LOOP_OFF;

        return sound_mode;
    }
}
