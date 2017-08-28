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
#include "../IO/StreamIO.h"
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	AudioSource::AudioSource()
	{
		m_filePath = NOT_ASSIGNED;
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
		{
			m_audioClip = g_context->GetSubsystem<Audio>()->CreateAudioClip();
		}
	
		// Set the transform
		m_audioClip._Get()->SetTransform(g_transform);
	}
	
	void AudioSource::Start()
	{
		// Make sure there is an audio clip
		if (m_audioClip.expired())
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
	
		m_audioClip._Get()->Stop();
	}
	
	void AudioSource::Update()
	{
		if (m_audioClip.expired())
			return;
	
		m_audioClip._Get()->Update();
	}
	
	void AudioSource::Serialize()
	{
		StreamIO::WriteSTR(m_filePath);
		StreamIO::WriteBool(m_mute);
		StreamIO::WriteBool(m_playOnAwake);
		StreamIO::WriteBool(m_loop);
		StreamIO::WriteInt(m_priority);
		StreamIO::WriteFloat(m_volume);
		StreamIO::WriteFloat(m_pitch);
		StreamIO::WriteFloat(m_pan);
	}
	
	void AudioSource::Deserialize()
	{
		m_filePath = StreamIO::ReadSTR();
		m_mute = StreamIO::ReadBool();
		m_playOnAwake = StreamIO::ReadBool();
		m_loop = StreamIO::ReadBool();
		m_priority = StreamIO::ReadInt();
		m_volume = StreamIO::ReadFloat();
		m_pitch = StreamIO::ReadFloat();
		m_pan = StreamIO::ReadFloat();
	
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
		{
			m_audioClip = g_context->GetSubsystem<Audio>()->CreateAudioClip();
		}
	
		// Load the audio (for now it's always in memory)
		m_audioClipLoaded = m_audioClip.lock()->Load(m_filePath, Memory);
	
		return m_audioClipLoaded;
	}
	
	string AudioSource::GetAudioClipName()
	{
		return FileSystem::GetFileNameFromFilePath(m_filePath);
	}
	
	bool AudioSource::PlayAudioClip()
	{
		if (m_audioClip.expired())
			return false;
	
		m_audioClip._Get()->Play();
		m_audioClip._Get()->SetMute(m_mute);
		m_audioClip._Get()->SetVolume(m_volume);
		m_audioClip._Get()->SetLoop(m_loop);
		m_audioClip._Get()->SetPriority(m_priority);
		m_audioClip._Get()->SetPan(m_pan);
	
		return true;
	}
	
	bool AudioSource::StopPlayingAudioClip()
	{
		if (m_audioClip.expired())
			return false;
	
		return m_audioClip._Get()->Stop();
	}
	
	void AudioSource::SetMute(bool mute)
	{
		if (m_audioClip.expired())
			return;
	
		m_audioClip._Get()->SetMute(mute);
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
		m_audioClip._Get()->SetVolume(m_volume);
	}
	
	void AudioSource::SetPitch(float pitch)
	{
		if (m_audioClip.expired())
			return;
	
		m_pitch = Clamp(pitch, 0.0f, 3.0f);
		m_audioClip._Get()->SetPitch(m_pitch);
	}
	
	void AudioSource::SetPan(float pan)
	{
		if (m_audioClip.expired())
			return;
	
		// Pan level, from -1.0 (left) to 1.0 (right).
		m_pan = Clamp(pan, -1.0f, 1.0f);
		m_audioClip._Get()->SetPan(m_pan);
	}
}
