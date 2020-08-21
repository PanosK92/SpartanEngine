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

//= INCLUDES =============================
#include "Spartan.h"
#include "AudioClip.h"
#include <fmod.hpp>
#include <fmod_errors.h>
#include "Audio.h"
#include "../World/Components/Transform.h"
#include "../IO/FileStream.h"
//========================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
using namespace FMOD;
//=============================

namespace Spartan
{
    AudioClip::AudioClip(Context* context) : IResource(context, ResourceType::Audio)
    {
        // AudioClip
        m_transform        = nullptr;
        m_systemFMOD    = static_cast<System*>(context->GetSubsystem<Audio>()->GetSystemFMOD());
        m_result        = FMOD_OK;
        m_soundFMOD        = nullptr;
        m_channelFMOD    = nullptr;
        m_playMode        = Play_Memory;
        m_minDistance    = 1.0f;
        m_maxDistance    = 10000.0f;
        m_modeRolloff    = FMOD_3D_LINEARROLLOFF;
        m_modeLoop        = FMOD_LOOP_OFF;
    }

    AudioClip::~AudioClip()
    {
        if (!m_soundFMOD)
            return;

        m_result = m_soundFMOD->release();
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
        }
    }

    bool AudioClip::LoadFromFile(const string& file_path)
    {
        m_soundFMOD     = nullptr;
        m_channelFMOD   = nullptr;

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

        return (m_playMode == Play_Memory) ? CreateSound(GetResourceFilePath()) : CreateStream(GetResourceFilePath());
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
        // Check if the sound is playing
        if (IsChannelValid())
        {
            auto is_playing = false;
            m_result = m_channelFMOD->isPlaying(&is_playing);
            if (m_result != FMOD_OK)
            {
                LogErrorFmod(m_result);
                return false;
            }
    
            // If it's already playing, don't bother
            if (is_playing)
                return true;
        }

        // Start playing the sound
        m_result = m_systemFMOD->playSound(m_soundFMOD, nullptr, false, &m_channelFMOD);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    bool AudioClip::Pause()
    {
        if (!IsChannelValid())
            return true;

        // Get sound paused state
        auto is_paused = false;
        m_result = m_channelFMOD->getPaused(&is_paused);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        // If it's already paused, don't bother
        if (!is_paused)
            return true;

        // Pause the sound
        m_result = m_channelFMOD->setPaused(true);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    bool AudioClip::Stop()
    {
        if (!IsChannelValid())
            return true;

        // If it's already stopped, don't bother
        if (!IsPlaying())
            return true;

        // Stop the sound
        m_result = m_channelFMOD->stop();
        if (m_result != FMOD_OK)
        {
            m_channelFMOD = nullptr;
            LogErrorFmod(m_result); // spams a lot
            return false;
        }

        m_channelFMOD = nullptr;

        return true;
    }

    bool AudioClip::SetLoop(const bool loop)
    {
        m_modeLoop = loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF;

        if (!m_soundFMOD)
            return false;

        // Infinite loops
        if (loop)
        {
            m_soundFMOD->setLoopCount(-1);
        }

        // Set the channel with the new mode
        m_result = m_soundFMOD->setMode(GetSoundMode());
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    bool AudioClip::SetVolume(float volume)
    {
        if (!IsChannelValid())
            return false;

        m_result = m_channelFMOD->setVolume(volume);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    bool AudioClip::SetMute(const bool mute)
    {
        if (!IsChannelValid())
            return false;

        m_result = m_channelFMOD->setMute(mute);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    bool AudioClip::SetPriority(const int priority)
    {
        if (!IsChannelValid())
            return false;

        m_result = m_channelFMOD->setPriority(priority);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    bool AudioClip::SetPitch(const float pitch)
    {
        if (!IsChannelValid())
            return false;

        m_result = m_channelFMOD->setPitch(pitch);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    bool AudioClip::SetPan(const float pan)
    {
        if (!IsChannelValid())
            return false;

        m_result = m_channelFMOD->setPan(pan);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    bool AudioClip::SetRolloff(const vector<Vector3>& curve_points)
    {
        if (!IsChannelValid())
            return false;

        SetRolloff(Custom);

        // Convert Vector3 to FMOD_VECTOR
        vector<FMOD_VECTOR> fmod_curve;
        for (const auto& point : curve_points)
        {
            fmod_curve.push_back(FMOD_VECTOR{ point.x, point.y, point.z });
        }

        m_result = m_channelFMOD->set3DCustomRolloff(&fmod_curve.front(), static_cast<int>(fmod_curve.size()));
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    bool AudioClip::SetRolloff(const Rolloff rolloff)
    {
        switch (rolloff)
        {
        case Linear: m_modeRolloff = FMOD_3D_LINEARROLLOFF;
            break;

        case Custom: m_modeRolloff = FMOD_3D_CUSTOMROLLOFF;
            break;

        default:
            break;
        }

        return true;
    }

    bool AudioClip::Update()
    {
        if (!IsChannelValid() || !m_transform)
            return true;

        const auto pos = m_transform->GetPosition();

        FMOD_VECTOR f_mod_pos = { pos.x, pos.y, pos.z };
        FMOD_VECTOR f_mod_vel = { 0, 0, 0 };

        // Set 3D attributes
        m_result = m_channelFMOD->set3DAttributes(&f_mod_pos, &f_mod_vel);
        if (m_result != FMOD_OK)
        {
            m_channelFMOD = nullptr;
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    bool AudioClip::IsPlaying()
    {
        if (!IsChannelValid())
            return false;

        auto is_playing = false;
        m_result = m_channelFMOD->isPlaying(&is_playing);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return is_playing;
    }

    //= CREATION ================================================
    bool AudioClip::CreateSound(const string& file_path)
    {
        // Create sound
        m_result = m_systemFMOD->createSound(file_path.c_str(), GetSoundMode(), nullptr, &m_soundFMOD);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        // Set 3D min max distance
        m_result = m_soundFMOD->set3DMinMaxDistance(m_minDistance, m_maxDistance);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    bool AudioClip::CreateStream(const string& file_path)
    {
        // Create sound
        m_result = m_systemFMOD->createStream(file_path.c_str(), GetSoundMode(), nullptr, &m_soundFMOD);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        // Set 3D min max distance
        m_result = m_soundFMOD->set3DMinMaxDistance(m_minDistance, m_maxDistance);
        if (m_result != FMOD_OK)
        {
            LogErrorFmod(m_result);
            return false;
        }

        return true;
    }

    int AudioClip::GetSoundMode() const
    {
        return FMOD_3D | m_modeLoop | m_modeRolloff;
    }

    void AudioClip::LogErrorFmod(int error) const
    {
        LOG_ERROR("%s", FMOD_ErrorString(static_cast<FMOD_RESULT>(error)));
    }

    bool AudioClip::IsChannelValid() const
    {
        if (!m_channelFMOD)
            return false;

        // Do a query and see if it fails or not
        bool value;
        return m_channelFMOD->isPlaying(&value) == FMOD_OK;
    }
    //===========================================================
}
