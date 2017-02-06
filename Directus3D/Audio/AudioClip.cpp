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

//= INCLUDES =====================
#include "AudioClip.h"
#include <fmod_errors.h>
#include "../Logging/Log.h"
#include "../Core/GUIDGenerator.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Directus;
using namespace Directus::Math;
//=============================

AudioClip::AudioClip(FMOD::System* fModSystem)
{
	// Resource
	m_resourceID = GENERATE_GUID;
	m_resourceType = Audio_Resource;

	// AudioClip
	m_transform = nullptr;
	m_fModSystem = fModSystem;
	m_result = FMOD_OK;
	m_sound = nullptr;
	m_channel = nullptr;
	m_playMode = Memory;
	m_minDistance = 1.0f;
	m_maxDistance = 10000.0f;
	m_modeRolloff = FMOD_3D_LINEARROLLOFF;
	m_modeLoop = FMOD_LOOP_OFF;
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

bool AudioClip::Load(const string& filePath, PlayMode mode)
{
	m_sound = nullptr;
	m_channel = nullptr;
	m_playMode = mode;

	return mode == Memory ? CreateSound(filePath) : CreateStream(filePath);
}

bool AudioClip::Play()
{
	// Check if the sound is playing
	if (m_channel)
	{
		bool isPlaying = false;
		m_result = m_channel->isPlaying(&isPlaying);
		if (m_result != FMOD_OK)
		{
			LOG_ERROR(FMOD_ErrorString(m_result));
			return false;
		}

		// If it's already playing, don't bother
		if (isPlaying)
			return true;
	}

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
	if (m_channel)
	{
		bool isPaused = false;
		m_result = m_channel->getPaused(&isPaused);
		if (m_result != FMOD_OK)
		{
			LOG_ERROR(FMOD_ErrorString(m_result));
			return false;
		}

		// If it's already stopped, don't bother
		if (!isPaused)
			return true;
	}

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
	if (m_channel)
	{
		bool isPlaying = false;
		m_result = m_channel->isPlaying(&isPlaying);
		if (m_result != FMOD_OK)
		{
			LOG_ERROR(FMOD_ErrorString(m_result));
			return false;
		}

		// If it's already stopped, don't bother
		if (!isPlaying)
			return true;
	}

	// Stop the sound
	m_result = m_channel->stop();
	m_channel = nullptr;
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	m_channel = nullptr;

	return true;
}

bool AudioClip::SetLoop(bool loop)
{
	m_modeLoop = loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF;

	if (!m_sound)
		return false;

	// Infite loops
	if (loop)
		m_sound->setLoopCount(-1);

	// Set the channel with the new mode
	m_result = m_sound->setMode(BuildSoundMode());
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
		return false;

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
		return false;

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
		return false;

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
		return false;

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
		return false;

	m_result = m_channel->setPan(pan);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::SetRolloff(vector<Vector3> curvePoints)
{
	SetRolloff(Custom);

	// Convert Vector3 to FMOD_VECTOR
	vector<FMOD_VECTOR> fmodCurve;
	for (const auto& point : curvePoints)
		fmodCurve.push_back(FMOD_VECTOR{ point.x, point.y, point.z });

	m_result = m_channel->set3DCustomRolloff(&fmodCurve.front(), (int)fmodCurve.size());
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::SetRolloff(Rolloff rolloff)
{
	switch (rolloff)
	{
	case Linear: m_modeRolloff = FMOD_3D_LINEARROLLOFF;
		break;

	case Custom: m_modeRolloff = FMOD_3D_CUSTOMROLLOFF;
		break;

	default:
		break;
	}

	return true;
}

bool AudioClip::Update()
{
	if (!m_transform || !m_channel)
		return false;

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
bool AudioClip::CreateSound(const string& filePath)
{
	// Create sound
	m_result = m_fModSystem->createSound(filePath.c_str(), BuildSoundMode(), nullptr, &m_sound);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	// Set 3D min max disance
	m_result = m_sound->set3DMinMaxDistance(m_minDistance, m_maxDistance);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool AudioClip::CreateStream(const string& filePath)
{
	// Create sound
	m_result = m_fModSystem->createStream(filePath.c_str(), BuildSoundMode(), nullptr, &m_sound);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	// Set 3D min max disance
	m_result = m_sound->set3DMinMaxDistance(m_minDistance, m_maxDistance);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}
//=========================================================================================