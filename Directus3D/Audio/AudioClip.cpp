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

//= INCLUDES ==============
#include "AudioClip.h"
#include <fmod_errors.h>
#include "../Logging/Log.h"
//=========================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

AudioClip::AudioClip(FMOD::System* fModSystem)
{
	m_transform = nullptr;
	m_fModSystem = fModSystem;
	m_result = FMOD_OK;
	m_sound = nullptr;
	m_channel = nullptr;
	m_distanceFactor = 1.0f;
	m_mode = Memory;
}

AudioClip::~AudioClip()
{
	if (!m_sound)
		return;

	m_result = m_sound->release();
	if (m_result != FMOD_OK)
		LOG_ERROR(FMOD_ErrorString(m_result));
}

bool AudioClip::SaveMetadata()
{
	return true;
}

bool AudioClip::Load(const std::string& filePath, PlayMode mode)
{
	m_sound = nullptr;
	m_channel = nullptr;
	m_mode = mode;

	return mode == Memory ? CreateSound(filePath) : CreateStream(filePath);
}

bool AudioClip::Play()
{
	// Check if the sound is playing
	bool isPlaying = false;
	if (m_channel)
	{
		m_result = m_channel->isPlaying(&isPlaying);
		if (m_result != FMOD_OK)
		{
			LOG_ERROR(FMOD_ErrorString(m_result));
			return false;
		}
	}

	// If it's already playing, don't bother
	if (isPlaying)
		return true;

	// Start playing the sound
	m_result = m_fModSystem->playSound(m_sound, nullptr, false, &m_channel);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::Pause()
{
	// Check if the sound is playing
	bool isPaused = false;
	if (m_channel)
	{
		m_result = m_channel->getPaused(&isPaused);
		if (m_result != FMOD_OK)
		{
			LOG_ERROR(FMOD_ErrorString(m_result));
			return false;
		}
	}

	// If it's already stopped, don't bother
	if (!isPaused)
		return true;

	// Stop the sound
	m_result = m_channel->setPaused(true);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::Stop()
{
	// Check if the sound is playing
	bool isPlaying = false;
	if (m_channel)
	{
		m_result = m_channel->isPlaying(&isPlaying);
		if (m_result != FMOD_OK)
		{
			LOG_ERROR(FMOD_ErrorString(m_result));
			return false;
		}
	}

	// If it's already stopped, don't bother
	if (!isPlaying)
		return true;

	// Stop the sound
	m_result = m_channel->stop();
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::SetLoop(bool loop)
{
	if (!m_channel)
		return true;

	// Get current mode
	FMOD_MODE mode;
	m_result = m_channel->getMode(&mode);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	// Adjust current mode based on loop variable
	if (loop)
		mode |= FMOD_LOOP_NORMAL;
	else
		mode &= ~FMOD_LOOP_OFF;

	// Se the new mode to the channel
	m_result = m_channel->setMode(mode);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::SetVolume(float volume)
{
	if (!m_channel)
		return true;

	m_result = m_channel->setVolume(volume);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::SetMute(bool mute)
{
	if (!m_channel)
		return true;

	m_result = m_channel->setMute(mute);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::SetPriority(int priority)
{
	if (!m_channel)
		return true;

	m_result = m_channel->setPriority(priority);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::SetPitch(float pitch)
{
	if (!m_channel)
		return true;

	m_result = m_channel->setPitch(pitch);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::SetPan(float pan)
{
	if (!m_channel)
		return true;

	m_result = m_channel->setPan(pan);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::Update()
{
	if (!m_transform || !m_channel)
		return true;

	Vector3 pos = m_transform->GetPosition();

	FMOD_VECTOR fModPos = { pos.x, pos.y, pos.z };
	FMOD_VECTOR fModVel = { 0, 0, 0 };

	// Set 3D attributes
	m_result = m_channel->set3DAttributes(&fModPos, &fModVel);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

void AudioClip::SetTransform(Transform* transform)
{
	m_transform = transform;
}

//= CREATION =====================================================================================
bool AudioClip::CreateSound(const std::string& filePath)
{
	// Create sound
	m_result = m_fModSystem->createSound(filePath.c_str(), FMOD_3D, nullptr, &m_sound);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	// Set 3D min max disance
	m_result = m_sound->set3DMinMaxDistance(0.5f * m_distanceFactor, 5000.0f * m_distanceFactor);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::CreateStream(const std::string& filePath)
{
	// Create sound
	m_result = m_fModSystem->createStream(filePath.c_str(), FMOD_3D, nullptr, &m_sound);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	// Set 3D min max disance
	m_result = m_sound->set3DMinMaxDistance(0.5f * m_distanceFactor, 5000.0f * m_distanceFactor);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}
//=============================================================================================