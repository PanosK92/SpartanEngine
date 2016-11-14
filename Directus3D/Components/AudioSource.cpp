/*
Copyright(c) 2016 Panos Karabelas

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
#include "../Logging/Log.h"
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

AudioSource::AudioSource()
{
	m_filePath = PATH_NOT_ASSIGNED;
	m_mute = false;
	m_playOnAwake = true;
	m_loop = false;
	m_priority = 128;
	m_volume = 1.0f;
	m_pitch = 1.0f;
	m_pan = 0.0f;
}

AudioSource::~AudioSource()
{

}

void AudioSource::Awake()
{
	// Get an audio handle (if there isn't one)
	if (m_audioHandle.expired())
		m_audioHandle = g_context->GetSubsystem<Audio>()->CreateAudioHandle();

	// TEMPORARY
	m_filePath = "Assets/Sounds/car.wav";

	if (FileSystem::IsSupportedAudioFile(m_filePath))
	{
		m_audioHandle.lock()->Load(m_filePath, Memory);
		m_audioHandle.lock()->SetTransform(g_transform);
	}
}

void AudioSource::Start()
{
	if (m_playOnAwake)
		m_audioHandle.lock()->Play();

	m_audioHandle.lock()->SetMute(m_mute);
	m_audioHandle.lock()->SetVolume(m_volume);
	m_audioHandle.lock()->SetLoop(m_loop);
}

void AudioSource::Remove()
{
	m_audioHandle.lock()->Stop();
}

void AudioSource::Update()
{
	if (m_audioHandle.expired())
		return;

	m_audioHandle.lock()->Update();
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
}

void AudioSource::SetMute(bool mute)
{
	if (m_audioHandle.expired())
		return;

	m_audioHandle.lock()->SetMute(mute);
}

void AudioSource::SetPriority(int priority)
{
	if (m_audioHandle.expired())
		return;

	// Priority for the channel, from 0 (most important) 
	// to 256 (least important), default = 128.
	m_priority = Clamp(priority, 0, 255);
	m_audioHandle.lock()->SetPriority(m_priority);
}

void AudioSource::SetVolume(float volume)
{
	if (m_audioHandle.expired())
		return;

	m_volume = Clamp(volume, 0.0f, 1.0f);
	m_audioHandle.lock()->SetVolume(m_volume);
}

void AudioSource::SetPitch(float pitch)
{
	if (m_audioHandle.expired())
		return;

	m_pitch = Clamp(pitch, 0.0f, 3.0f);
	m_audioHandle.lock()->SetPitch(m_pitch);
}

void AudioSource::SetPan(float pan)
{
	if (m_audioHandle.expired())
		return;

	// Pan level, from -1.0 (left) to 1.0 (right).
	m_pan = Clamp(pan, -1.0f, 1.0f);
	m_audioHandle.lock()->SetPan(m_pan);
}
