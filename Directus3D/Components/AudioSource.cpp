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
	m_audio = nullptr;
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
	m_audio = g_context->GetSubsystem<Audio>();
	m_filePath = "Assets/Sounds/music.mp3";
	g_context->GetSubsystem<Audio>()->CreateStream(m_filePath);
	g_context->GetSubsystem<Audio>()->Play(m_filePath);
}

void AudioSource::Start()
{

}

void AudioSource::Remove()
{
	g_context->GetSubsystem<Audio>()->Stop(m_filePath);
}

void AudioSource::Update()
{
	if (!m_audio)
		return;

	m_audio->SetAudioSourceTransform(m_filePath, g_transform);
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
	m_volume = Clamp(volume, 0.0, 1.0f);
	g_context->GetSubsystem<Audio>()->SetVolume(m_volume, m_filePath);
}
