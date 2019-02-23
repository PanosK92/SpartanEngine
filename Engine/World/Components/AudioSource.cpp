/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "AudioSource.h"
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
//=======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
using namespace Helper;
//=============================

namespace Directus
{
	AudioSource::AudioSource(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{
		m_filePath			= NOT_ASSIGNED;
		m_mute				= false;
		m_playOnStart		= true;
		m_loop				= false;
		m_priority			= 128;
		m_volume			= 1.0f;
		m_pitch				= 1.0f;
		m_pan				= 0.0f;
		m_audioClipLoaded	= false;
	}
	
	AudioSource::~AudioSource()
	{
	
	}
	
	void AudioSource::OnInitialize()
	{
		if (!m_audioClip)
			return;
		
		// Set the transform
		m_audioClip->SetTransform(GetTransform());
	}
	
	void AudioSource::OnStart()
	{
		if (!m_playOnStart)
			return;

		Play();
	}
	
	void AudioSource::OnStop()
	{
		Stop();
	}
	
	void AudioSource::OnRemove()
	{
		if (!m_audioClip)
			return;
	
		m_audioClip->Stop();
	}
	
	void AudioSource::OnTick()
	{
		if (!m_audioClip)
			return;
	
		m_audioClip->Update();
	}
	
	void AudioSource::Serialize(FileStream* stream)
	{
		stream->Write(m_filePath);
		stream->Write(m_mute);
		stream->Write(m_playOnStart);
		stream->Write(m_loop);
		stream->Write(m_priority);
		stream->Write(m_volume);
		stream->Write(m_pitch);
		stream->Write(m_pan);
	}
	
	void AudioSource::Deserialize(FileStream* stream)
	{
		stream->Read(&m_filePath);
		stream->Read(&m_mute);
		stream->Read(&m_playOnStart);
		stream->Read(&m_loop);
		stream->Read(&m_priority);
		stream->Read(&m_volume);
		stream->Read(&m_pitch);
		stream->Read(&m_pan);
	
		// ResourceManager will return cached audio clip if it's already loaded
		m_audioClip = m_context->GetSubsystem<ResourceCache>()->Load<AudioClip>(m_filePath);
	}

	void AudioSource::SetAudioClip(const shared_ptr<AudioClip>& audioClip)
	{
		if (!audioClip)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}
		m_audioClip = audioClip;
	}

	const string& AudioSource::GetAudioClipName()
	{
		return m_audioClip ? m_audioClip->GetResourceName() : NOT_ASSIGNED;
	}
	
	bool AudioSource::Play()
	{
		if (!m_audioClip)
			return false;
	
		m_audioClip->Play();
		m_audioClip->SetMute(m_mute);
		m_audioClip->SetVolume(m_volume);
		m_audioClip->SetLoop(m_loop);
		m_audioClip->SetPriority(m_priority);
		m_audioClip->SetPan(m_pan);
	
		return true;
	}
	
	bool AudioSource::Stop()
	{
		if (!m_audioClip)
			return false;
	
		return m_audioClip->Stop();
	}
	
	void AudioSource::SetMute(bool mute)
	{
		if (m_mute == mute || !m_audioClip)
			return;
	
		m_mute = mute;
		m_audioClip->SetMute(mute);
	}
	
	void AudioSource::SetPriority(int priority)
	{
		if (!m_audioClip)
			return;
	
		// Priority for the channel, from 0 (most important) 
		// to 256 (least important), default = 128.
		m_priority = (int)Clamp(priority, 0, 255);
		m_audioClip->SetPriority(m_priority);
	}
	
	void AudioSource::SetVolume(float volume)
	{
		if (!m_audioClip)
			return;
	
		m_volume = Clamp(volume, 0.0f, 1.0f);
		m_audioClip->SetVolume(m_volume);
	}
	
	void AudioSource::SetPitch(float pitch)
	{
		if (!m_audioClip)
			return;
	
		m_pitch = Clamp(pitch, 0.0f, 3.0f);
		m_audioClip->SetPitch(m_pitch);
	}
	
	void AudioSource::SetPan(float pan)
	{
		if (!m_audioClip)
			return;
	
		// Pan level, from -1.0 (left) to 1.0 (right).
		m_pan = Clamp(pan, -1.0f, 1.0f);
		m_audioClip->SetPan(m_pan);
	}
}
