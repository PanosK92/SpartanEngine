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

//= INCLUDES ===============
#include "AudioSource.h"
#include "../Core/Context.h"
#include "../Audio/Audio.h"
//==========================

AudioSource::AudioSource()
{
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
	m_filePath = "Assets/Sounds/music.mp3";
	g_context->GetSubsystem<Audio>()->LoadSound(m_filePath);
	g_context->GetSubsystem<Audio>()->Play(m_filePath);
}

void AudioSource::Start()
{

}

void AudioSource::Remove()
{

}

void AudioSource::Update()
{

}

void AudioSource::Serialize()
{

}

void AudioSource::Deserialize()
{

}
