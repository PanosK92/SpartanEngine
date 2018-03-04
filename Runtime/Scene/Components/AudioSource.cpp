/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ===========================
#include "AudioSource.h"
#include "../../Core/Context.h"
#include "../../Audio/Audio.h"
#include "../../FileSystem/FileSystem.h"
#include "../../IO/FileStream.h"
//======================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	AudioSource::AudioSource(Context* context, GameObject* gameObject, Transform* transform) : IComponent(context, gameObject, transform)
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
		// Get an audio handle (in case there isn't one yet)
		if (m_audioClip.expired())
		{
			m_audioClip = GetContext()->GetSubsystem<Audio>()->CreateAudioClip();
		}
	
		// Set the transform
		m_audioClip.lock()->SetTransform(GetTransform());
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
		if (m_audioClip.expired())
			return;
	
		m_audioClip.lock()->Stop();
	}
	
	void AudioSource::OnUpdate()
	{
		if (m_audioClip.expired())
			return;
	
		m_audioClip.lock()->Update();
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
	
		LoadAudioClip(m_filePath);
	}

	bool AudioSource::SetAudioClip(weak_ptr<AudioClip> audioClip, bool cacheIt)
	{
		if (audioClip.expired())
		{
			m_audioClip = audioClip;
			return true;

		}
		m_audioClip = !cacheIt ? audioClip : audioClip.lock()->Cache<AudioClip>();
		return true;
	}

	string AudioSource::GetAudioClipName()
	{
		return FileSystem::GetFileNameFromFilePath(m_filePath);
	}
	
	bool AudioSource::Play()
	{
		if (m_audioClip.expired())
			return false;
	
		auto audioClip = m_audioClip.lock();
		audioClip->Play();
		audioClip->SetMute(m_mute);
		audioClip->SetVolume(m_volume);
		audioClip->SetLoop(m_loop);
		audioClip->SetPriority(m_priority);
		audioClip->SetPan(m_pan);
	
		return true;
	}
	
	bool AudioSource::Stop()
	{
		if (m_audioClip.expired())
			return false;
	
		return m_audioClip.lock()->Stop();
	}
	
	void AudioSource::SetMute(bool mute)
	{
		if (m_audioClip.expired())
			return;
	
		m_audioClip.lock()->SetMute(mute);
	}
	
	void AudioSource::SetPriority(int priority)
	{
		if (m_audioClip.expired())
			return;
	
		// Priority for the channel, from 0 (most important) 
		// to 256 (least important), default = 128.
		m_priority = (int)Clamp(priority, 0, 255);
		m_audioClip.lock()->SetPriority(m_priority);
	}
	
	void AudioSource::SetVolume(float volume)
	{
		if (m_audioClip.expired())
			return;
	
		m_volume = Clamp(volume, 0.0f, 1.0f);
		m_audioClip.lock()->SetVolume(m_volume);
	}
	
	void AudioSource::SetPitch(float pitch)
	{
		if (m_audioClip.expired())
			return;
	
		m_pitch = Clamp(pitch, 0.0f, 3.0f);
		m_audioClip.lock()->SetPitch(m_pitch);
	}
	
	void AudioSource::SetPan(float pan)
	{
		if (m_audioClip.expired())
			return;
	
		// Pan level, from -1.0 (left) to 1.0 (right).
		m_pan = Clamp(pan, -1.0f, 1.0f);
		m_audioClip.lock()->SetPan(m_pan);
	}

	bool AudioSource::LoadAudioClip(const string& filePath)
	{
		m_filePath = filePath;
	
		// Make sure the filePath points to an actual playble audio file
		if (!FileSystem::IsSupportedAudioFile(m_filePath))
			return false;
	
		// If there is audio clip handle, create one
		if (m_audioClip.expired())
		{
			m_audioClip = GetContext()->GetSubsystem<Audio>()->CreateAudioClip();
		}
	
		// Load the audio (for now it's always in memory)
		m_audioClipLoaded = m_audioClip.lock()->LoadFromFile(m_filePath);
	
		return m_audioClipLoaded;
	}
}
