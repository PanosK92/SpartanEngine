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
    AudioClip::AudioClip(Context* context) : IResource(context, ResourceType::Audio)
    {
        m_modeRolloff = FMOD_3D_LINEARROLLOFF;
        m_modeLoop    = FMOD_LOOP_OFF;
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

    bool AudioClip::Play()
    {
        if (IsPlaying())
            return false;

        return Audio::PlaySound(m_fmod_sound, m_fmod_channel);
    }

    bool AudioClip::Pause()
    {
        if (!IsPaused())
            return false;

        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setPaused(true));
        }

        return false;
    }

    bool AudioClip::Stop()
    {
        if (!IsPlaying())
            return false;

        if (FMOD::Channel* channel = static_cast<FMOD::Channel*>(m_fmod_channel))
        {
            return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->stop());
        }

        return false;
    }

    bool AudioClip::SetLoop(const bool loop)
    {
        m_modeLoop = loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF;

        if (!static_cast<FMOD::Sound*>(m_fmod_sound))
            return false;

        // Infinite loops
        if (loop)
        {
            static_cast<FMOD::Sound*>(m_fmod_sound)->setLoopCount(-1);
        }

        return Audio::HandleErrorFmod(static_cast<FMOD::Sound*>(m_fmod_sound)->setMode(GetSoundMode()));
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
            return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->setPan(pan));
        }

        return false;
    }

    bool AudioClip::SetRolloff(const vector<Vector3>& curve_points)
    {
        SetRolloff(Rolloff::Custom);

        // Convert Vector3 to FMOD_VECTOR
        vector<FMOD_VECTOR> fmod_curve;
        for (const auto& point : curve_points)
        {
            fmod_curve.push_back(FMOD_VECTOR{ point.x, point.y, point.z });
        }

        return Audio::HandleErrorFmod(static_cast<FMOD::Channel*>(m_fmod_channel)->set3DCustomRolloff(&fmod_curve.front(), static_cast<int>(fmod_curve.size())));
    }

    bool AudioClip::SetRolloff(const Rolloff rolloff)
    {
        switch (rolloff)
        {
        case Rolloff::Linear: m_modeRolloff = FMOD_3D_LINEARROLLOFF;
            break;

        case Rolloff::Custom: m_modeRolloff = FMOD_3D_CUSTOMROLLOFF;
            break;

        default:
            break;
        }

        return true;
    }

    bool AudioClip::Update()
    {
        if (!m_transform)
            return true;

        const auto pos = m_transform->GetPosition();

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
        if (!Audio::HandleErrorFmod(static_cast<FMOD::Sound*>(m_fmod_sound)->set3DMinMaxDistance(m_minDistance, m_maxDistance)))
            return false;

        return true;
    }

    bool AudioClip::CreateStream(const string& file_path)
    {
        // Create sound
        if (!Audio::CreateStream(file_path, GetSoundMode(), m_fmod_sound))
            return false;

        // Set 3D min max distance
        if (!Audio::HandleErrorFmod(static_cast<FMOD::Sound*>(m_fmod_sound)->set3DMinMaxDistance(m_minDistance, m_maxDistance)))
            return false;

        return true;
    }

    int AudioClip::GetSoundMode() const
    {
        return FMOD_3D | m_modeLoop | m_modeRolloff;
    }
}
