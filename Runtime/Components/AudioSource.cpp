/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ========================
#include "AudioSource.h"
#include "../Core/Context.h"
#include "../Audio/Audio.h"
#include "../FileSystem/FileSystem.h"
#include "../IO/Serializer.h"
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	AudioSource::AudioSource()
	{
		m_filePath = DATA_NOT_ASSIGNED;
		m_mute = false;
		m_playOnAwake = true;
		m_loop = false;
		m_priority = 128;
		m_volume = 1.0f;
		m_pitch = 1.0f;
		m_pan = 0.0f;
		m_audioClipLoaded = false;
	}
	
	AudioSource::~AudioSource()
	{
	
	}
	
	void AudioSource::Reset()
	{
		// Get an audio handle (in case there isn't one yet)
		if (m_audioClip.expired())
			m_audioClip = g_context->GetSubsystem<Audio>()->CreateAudioClip();
	
		// Set the transform
		m_audioClip.lock()->SetTransform(g_transform);
	}
	
	void AudioSource::Start()
	{
		auto audioClip = m_audioClip.lock();
	
		// Make sure there is an audio clip
		if (!audioClip)
			return;
	
		// Make sure it's an actual playble audio file
		if (!FileSystem::IsSupportedAudioFile(m_filePath))
			return;
	
		if (!m_audioClipLoaded)
			return;
	
		// Start playing the audio file
		if (m_playOnAwake)
			PlayAudioClip();
	}
	
	void AudioSource::OnDisable()
	{
		StopPlayingAudioClip();
	}
	
	void AudioSource::Remove()
	{
		if (m_audioClip.expired())
			return;
	
		m_audioClip.lock()->Stop();
	}
	
	void AudioSource::Update()
	{
		if (m_audioClip.expired())
			return;
	
		m_audioClip.lock()->Update();
	}
	
	void AudioSource::Serialize()
	{
		Serializer::WriteSTR(m_filePath);
		Serializer::WriteBool(m_mute);
		Serializer::WriteBool(m_playOnAwake);
		Serializer::WriteBool(m_loop);
		Serializer::WriteInt(m_priority);
		Serializer::WriteFloat(m_volume);
		Serializer::WriteFloat(m_pitch);
		Serializer::WriteFloat(m_pan);
	}
	
	void AudioSource::Deserialize()
	{
		m_filePath = Serializer::ReadSTR();
		m_mute = Serializer::ReadBool();
		m_playOnAwake = Serializer::ReadBool();
		m_loop = Serializer::ReadBool();
		m_priority = Serializer::ReadInt();
		m_volume = Serializer::ReadFloat();
		m_pitch = Serializer::ReadFloat();
		m_pan = Serializer::ReadFloat();
	
		LoadAudioClip(m_filePath);
	}
	
	bool AudioSource::LoadAudioClip(const string& filePath)
	{
		m_filePath = filePath;
	
		// Make sure the filePath points to an actual playble audio file
		if (!FileSystem::IsSupportedAudioFile(m_filePath))
			return false;
	
		// If there is audio clip handle, create one
		if (m_audioClip.expired())
			m_audioClip = g_context->GetSubsystem<Audio>()->CreateAudioClip();
	
		// Load the audio (for now it's always in memory)
		m_audioClipLoaded = m_audioClip.lock()->Load(m_filePath, Memory);
	
		return m_audioClipLoaded;
	}
	
	string AudioSource::GetAudioClipName()
	{
		return FileSystem::GetFileNameFromPath(m_filePath);
	}
	
	bool AudioSource::PlayAudioClip()
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
	
	bool AudioSource::StopPlayingAudioClip()
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
}
