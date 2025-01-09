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

//= INCLUDES ============================
#include "pch.h"
#include "AudioSource.h"
#include "../../Audio/AudioClip.h"
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::Math;
//============================

namespace spartan
{
    AudioSource::AudioSource(Entity* entity) : Component(entity)
    {

    }
    
    AudioSource::~AudioSource()
    {
        Stop();
    }

    void AudioSource::OnInitialize()
    {
        Component::OnInitialize();

        if (!m_audio_clip)
            return;
        
        m_audio_clip->SetTransform(GetEntity());
    }
    
    void AudioSource::OnStart()
    {
        if (!m_play_on_start)
            return;

        Play();
    }
    
    void AudioSource::OnStop()
    {
        Stop();
    }
    
    void AudioSource::OnRemove()
    {
        if (!m_audio_clip)
            return;
    
        m_audio_clip->Stop();
    }
    
    void AudioSource::OnTick()
    {
        if (!m_audio_clip)
            return;

        m_audio_clip->Update();
    }
    
    void AudioSource::Serialize(FileStream* stream)
    {
        stream->Write(m_mute);
        stream->Write(m_loop);
        stream->Write(m_3d);
        stream->Write(m_play_on_start);
        stream->Write(m_priority);
        stream->Write(m_volume);
        stream->Write(m_pitch);
        stream->Write(m_pan);

        const bool has_audio_clip = m_audio_clip != nullptr;
        stream->Write(has_audio_clip);
        if (has_audio_clip)
        {
            stream->Write(m_audio_clip->GetObjectName());
        }
    }
    
    void AudioSource::Deserialize(FileStream* stream)
    {
        stream->Read(&m_mute);
        stream->Read(&m_loop);
        stream->Read(&m_3d);
        stream->Read(&m_play_on_start);
        stream->Read(&m_priority);
        stream->Read(&m_volume);
        stream->Read(&m_pitch);
        stream->Read(&m_pan);

        if (stream->ReadAs<bool>())
        {
            m_audio_clip = ResourceCache::GetByName<AudioClip>(stream->ReadAs<string>());
        }
    }

    void AudioSource::SetAudioClip(const string& file_path)
    {
        if (!FileSystem::IsFile(file_path))
        {
            SP_LOG_ERROR("\"%s\" doesn't point to a file", file_path.c_str());
            return;
        }

        m_audio_clip = ResourceCache::Load<AudioClip>(file_path);
    }

    string AudioSource::GetAudioClipName() const
    {
        return m_audio_clip ? m_audio_clip->GetObjectName() : "";
    }
    
	bool AudioSource::IsPlaying() const
	{
        if (!m_audio_clip)
            return false;

        return m_audio_clip->IsPlaying();
	}

	void AudioSource::Play() const
    {
        if (!m_audio_clip)
            return;
    
        m_audio_clip->Play(m_loop, m_3d);
        m_audio_clip->SetMute(m_mute);
        m_audio_clip->SetVolume(m_volume);
        m_audio_clip->SetPriority(m_priority);
        m_audio_clip->SetPan(m_pan);
    }
    
    void AudioSource::Stop() const
    {
        if (!m_audio_clip)
            return;
    
        m_audio_clip->Stop();
    }

    float AudioSource::GetProgress() const
    {
        if (!m_audio_clip)
            return 0.0f;
    
        return m_audio_clip->GetProgress();
    }
    
    void AudioSource::SetMute(bool mute)
    {
        if (m_mute == mute || !m_audio_clip)
            return;
    
        m_mute = mute;
        m_audio_clip->SetMute(mute);
    }

    void AudioSource::SetLoop(const bool loop)
	{
        if (!m_audio_clip)
            return;

        m_loop = loop;
        m_audio_clip->SetLoop(loop);
	}

	void AudioSource::SetPriority(int priority)
    {
        if (!m_audio_clip)
            return;
    
        // Priority for the channel, from 0 (most important) 
        // to 256 (least important), default = 128.
        m_priority = static_cast<int>(Helper::Clamp(priority, 0, 255));
        m_audio_clip->SetPriority(m_priority);
    }
    
    void AudioSource::SetVolume(float volume)
    {
        if (!m_audio_clip)
            return;
    
        m_volume = Helper::Clamp(volume, 0.0f, 1.0f);
        m_audio_clip->SetVolume(m_volume);
    }
    
    void AudioSource::SetPitch(float pitch)
    {
        if (!m_audio_clip)
            return;
    
        m_pitch = Helper::Clamp(pitch, 0.0f, 3.0f);
        m_audio_clip->SetPitch(m_pitch);
    }
    
    void AudioSource::SetPan(float pan)
    {
        if (!m_audio_clip)
            return;
    
        // Pan level, from -1.0 (left) to 1.0 (right).
        m_pan = Helper::Clamp(pan, -1.0f, 1.0f);
        m_audio_clip->SetPan(m_pan);
    }

    void AudioSource::Set3d(const bool enabled)
    {
        if (!m_audio_clip)
            return;

        m_3d = enabled;
        return m_audio_clip->Set3d(enabled);
    }
}
