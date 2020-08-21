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

//= INCLUDES ============================
#include "Spartan.h"
#include "AudioSource.h"
#include "../../Audio/AudioClip.h"
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    AudioSource::AudioSource(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id)
    {
        m_mute                = false;
        m_play_on_start        = true;
        m_loop                = false;
        m_priority            = 128;
        m_volume            = 1.0f;
        m_pitch                = 1.0f;
        m_pan                = 0.0f;
        m_audio_clip_loaded    = false;
    }
    
    void AudioSource::OnInitialize()
    {
        if (!m_audio_clip)
            return;
        
        // Set the transform
        m_audio_clip->SetTransform(GetTransform());
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
    
    void AudioSource::OnTick(float delta_time)
    {
        if (!m_audio_clip)
            return;
    
        m_audio_clip->Update();
    }
    
    void AudioSource::Serialize(FileStream* stream)
    {
        stream->Write(m_mute);
        stream->Write(m_play_on_start);
        stream->Write(m_loop);
        stream->Write(m_priority);
        stream->Write(m_volume);
        stream->Write(m_pitch);
        stream->Write(m_pan);

        const bool has_audio_clip = m_audio_clip != nullptr;
        stream->Write(has_audio_clip);
        if (has_audio_clip)
        {
            stream->Write(m_audio_clip->GetResourceName());
        }
    }
    
    void AudioSource::Deserialize(FileStream* stream)
    {
        stream->Read(&m_mute);
        stream->Read(&m_play_on_start);
        stream->Read(&m_loop);
        stream->Read(&m_priority);
        stream->Read(&m_volume);
        stream->Read(&m_pitch);
        stream->Read(&m_pan);

        if (stream->ReadAs<bool>())
        {
            m_audio_clip = m_context->GetSubsystem<ResourceCache>()->GetByName<AudioClip>(stream->ReadAs<string>());
        }
    }

    void AudioSource::SetAudioClip(const string& file_path)
    {
        // Create and load the audio clip
        auto audio_clip = make_shared<AudioClip>(m_context);
        if (audio_clip->LoadFromFile(file_path))
        {
            // In order for the component to guarantee serialization/deserialization, we cache the audio clip
            m_audio_clip = m_context->GetSubsystem<ResourceCache>()->Cache(audio_clip);
        }
    }

    string AudioSource::GetAudioClipName() const
    {
        return m_audio_clip ? m_audio_clip->GetResourceName() : "";
    }
    
    bool AudioSource::Play() const
    {
        if (!m_audio_clip)
            return false;
    
        m_audio_clip->Play();
        m_audio_clip->SetMute(m_mute);
        m_audio_clip->SetVolume(m_volume);
        m_audio_clip->SetLoop(m_loop);
        m_audio_clip->SetPriority(m_priority);
        m_audio_clip->SetPan(m_pan);
    
        return true;
    }
    
    bool AudioSource::Stop() const
    {
        if (!m_audio_clip)
            return false;
    
        return m_audio_clip->Stop();
    }
    
    void AudioSource::SetMute(bool mute)
    {
        if (m_mute == mute || !m_audio_clip)
            return;
    
        m_mute = mute;
        m_audio_clip->SetMute(mute);
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
}
