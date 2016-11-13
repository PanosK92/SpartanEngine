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
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

AudioSource::AudioSource()
{
	m_filePath = PATH_NOT_ASSIGNED;
	m_mute = false;
	m_volume = 1.0f;
	m_playOnAwake = true;
	m_loop = false;
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
	m_filePath = "Assets/Sounds/music.mp3";

	if (FileSystem::IsSupportedAudioFile(m_filePath))
	{
		m_audioHandle.lock()->Load(m_filePath, Memory);
		m_audioHandle.lock()->SetTransform(g_transform);
	}
}

void AudioSource::Start()
{
	if (!m_playOnAwake)
		return;

	m_audioHandle.lock()->Play();
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
	Serializer::WriteFloat(m_volume);
	Serializer::WriteBool(m_playOnAwake);
	Serializer::WriteBool(m_loop);
}

void AudioSource::Deserialize()
{
	m_filePath = Serializer::ReadSTR();
	m_mute = Serializer::ReadBool();
	m_volume = Serializer::ReadFloat();
	m_playOnAwake = Serializer::ReadBool();
	m_loop = Serializer::ReadBool();
}

void AudioSource::SetVolume(float volume)
{
	if (m_audioHandle.expired())
		return;

	m_volume = Clamp(volume, 0.0, 1.0f);
	m_audioHandle.lock()->SetVolume(m_volume);
}
