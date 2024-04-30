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

//= INCLUDES ================
#include "pch.h"
#include "AudioClip.h"
#include "Audio.h"
#include "../IO/FileStream.h"
#include "../World/Entity.h"
#if defined(_MSC_VER)
#include <fmod.hpp>
#endif
//===========================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    namespace
    {
        FMOD_RESULT F_CALLBACK channel_callback(FMOD_CHANNELCONTROL* channelcontrol, FMOD_CHANNELCONTROL_TYPE controlType, FMOD_CHANNELCONTROL_CALLBACK_TYPE callbackType, void* commandData1, void* commandData2)
        {
            if (controlType == FMOD_CHANNELCONTROL_CHANNEL && callbackType == FMOD_CHANNELCONTROL_CALLBACK_END)
            {
                FMOD::Channel* channel = reinterpret_cast<FMOD::Channel*>(channelcontrol);

                // retrieve the user data
                AudioClip* audio_clip = nullptr;
                channel->getUserData(reinterpret_cast<void**>(&audio_clip));

                // nullify the channel
                if (audio_clip)
                {
                    audio_clip->ResetChannel();
                }
            }

            return FMOD_OK;
        }

        unsigned int estimate_memory_usage(FMOD::Sound* sound)
        {
            SP_ASSERT(sound != nullptr);

            unsigned int length = 0;
            sound->getLength(&length, FMOD_TIMEUNIT_PCM);

            FMOD_SOUND_FORMAT format = FMOD_SOUND_FORMAT_NONE;
            int channels             = 0;
            int bits                 = 0;
            sound->getFormat(nullptr, &format, &channels, &bits);

            return length * channels * (bits / 8);
        }
    }

    AudioClip::AudioClip() : IResource(ResourceType::Audio)
    {

    }

    AudioClip::~AudioClip()
    {
        #if defined(_MSC_VER)

        if (FMOD::Sound* sound = static_cast<FMOD::Sound*>(m_fmod_sound))
        {
            Audio::HandleErrorFmod(sound->release());
        }

        #endif
    }

    bool AudioClip::LoadFromFile(const string& file_path)
    {
        bool loaded = false;

        #if defined(_MSC_VER)

        // native
        if (FileSystem::GetExtensionFromFilePath(file_path) == EXTENSION_AUDIO)
        {
            auto file = make_unique<FileStream>(file_path, FileStream_Read);
            if (!file->IsOpen())
                return false;

            SetResourceFilePath(file->ReadAs<string>());

            file->Close();
        }
        // foreign
        else
        {
            SetResourceFilePath(file_path);
        }

        // load
        loaded        = (m_playMode == PlayMode::Memory) ? CreateSound(GetResourceFilePath()) : CreateStream(GetResourceFilePath());
        m_object_size = estimate_memory_usage(static_cast<FMOD::Sound*>(m_fmod_sound));

        #endif

        return loaded;
    }

    bool AudioClip::SaveToFile(const string& file_path)
    {
        #if defined(_MSC_VER)

        auto file = make_unique<FileStream>(file_path, FileStream_Write);
        if (!file->IsOpen())
            return false;

        file->Write(GetResourceFilePath());

        file->Close();
        #endif

        return true;
    }

    void AudioClip::Play(const bool loop, const bool is_3d)
    {
        #if defined(_MSC_VER)

        if (IsPlaying())
            return;
 
        Audio::PlaySound(m_fmod_sound, m_fmod_channel);

        // set callback
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            channel->setUserData(this);
            channel->setCallback(channel_callback);
        }

        SetLoop(loop);
        Set3d(is_3d);

        #endif
    }

    void AudioClip::Pause()
    {
        #if defined(_MSC_VER)

        if (!IsPaused())
            return;

        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setPaused(true));
        }

        #endif
    }

    void AudioClip::Stop()
    {
        #if defined(_MSC_VER)
        if (!IsPlaying())
            return;

        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->stop());
        }
        #endif
    }

    bool AudioClip::GetLoop() const
    {
        #if defined(_MSC_VER)
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            FMOD_MODE current_mode;
            channel->getMode(&current_mode);

            return (current_mode & FMOD_LOOP_NORMAL) != 0;
        }

        #endif
        return false;
    }

    void AudioClip::SetLoop(const bool loop)
    {
        #if defined(_MSC_VER)
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
        #endif
    }

    bool AudioClip::SetVolume(float volume)
    {
        #if defined(_MSC_VER)
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setVolume(volume));
        }
        #endif

        return false;
    }

    bool AudioClip::SetMute(const bool mute)
    {
        #if defined(_MSC_VER)
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setMute(mute));
        }
        #endif

        return false;
    }

    bool AudioClip::SetPriority(const int priority)
    {
        #if defined(_MSC_VER)
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setPriority(priority));
        }
        #endif

        return false;
    }

    bool AudioClip::SetPitch(const float pitch)
    {
        #if defined(_MSC_VER)
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setPitch(pitch));
        }
        #endif

        return false;
    }

    bool AudioClip::SetPan(const float pan)
    {
        #if defined(_MSC_VER)
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(channel->setPan(pan));
        }
        #endif

        return false;
    }

    void AudioClip::Set3d(const bool enabled)
    {
        #if defined(_MSC_VER)
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            channel->setMode(enabled ? FMOD_3D : FMOD_2D);
        }
        #endif
    }

    bool AudioClip::Get3d() const
    {
        #if defined(_MSC_VER)
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            FMOD_MODE current_mode;
            channel->getMode(&current_mode);

            // returns true if FMOD_3D is set, false otherwise
            return (current_mode & FMOD_3D) != 0; 
        }
        #endif

        // Default or error value
        return false;
    }

    bool AudioClip::Update()
    {
        #if defined(_MSC_VER)
        if (!m_entity)
            return true;

        const Vector3 pos = m_entity->GetPosition();

        FMOD_VECTOR f_mod_pos = { pos.x, pos.y, pos.z };
        FMOD_VECTOR f_mod_vel = { 0, 0, 0 };

        return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->set3DAttributes(&f_mod_pos, &f_mod_vel));
        #else
        return true;
        #endif
    }

    bool AudioClip::IsPlaying()
    {
        bool is_playing = false;

        #if defined(_MSC_VER)
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            Audio::HandleErrorFmod(channel->isPlaying(&is_playing));
        }
        #endif

        return is_playing;
    }

    bool AudioClip::IsPaused()
    {
        bool is_paused = false;

        #if defined(_MSC_VER)
        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            Audio::HandleErrorFmod(channel->getPaused(&is_paused));
        }
        #endif

        return is_paused;
    }

    bool AudioClip::CreateSound(const string& file_path)
    {
        #if defined(_MSC_VER)
        // Create sound
        if (!Audio::CreateSound(file_path, GetSoundMode(), m_fmod_sound))
            return false;

        // Set 3D min max distance
        if (!Audio::HandleErrorFmod(static_cast<FMOD::Sound*>(m_fmod_sound)->set3DMinMaxDistance(m_distance_min, m_distance_max)))
            return false;

        #endif
        return true;
    }

    bool AudioClip::CreateStream(const string& file_path)
    {
        #if defined(_MSC_VER)
        // Create sound
        if (!Audio::CreateStream(file_path, GetSoundMode(), m_fmod_sound))
            return false;

        // Set 3D min max distance
        if (!Audio::HandleErrorFmod(static_cast<FMOD::Sound*>(m_fmod_sound)->set3DMinMaxDistance(m_distance_min, m_distance_max)))
            return false;
        #endif

        return true;
    }

    int AudioClip::GetSoundMode() const
    {
        unsigned int sound_mode  = 0;
        #if defined(_MSC_VER)
        sound_mode              |= FMOD_2D;
        sound_mode              |= FMOD_3D_LINEARROLLOFF;
        sound_mode              |= FMOD_LOOP_OFF;

        #endif
        return sound_mode;
    }
}
