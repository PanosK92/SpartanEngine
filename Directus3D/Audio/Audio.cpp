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

//= INCLUDES ==============
#include "Audio.h"
#include "fmod_errors.h"
#include "../Logging/Log.h"
//=========================

//= NAMESPACES ======
using namespace std;
using namespace FMOD;
//===================

Audio::Audio(Context* context) : Object(context)
{
	m_result = FMOD_OK;
	m_fmodSystem = nullptr;
	m_maxChannels = 32;
	m_distanceFactor = 1.0f;
	m_initialized = false;

	m_listener = nullptr;
	m_pos = { 0, 0, 0 };
	m_vel = { 0, 0, 0 };
	m_for = { 0, 0, -1 };
	m_up = { 0, 1, 0 };
}

Audio::~Audio()
{
	if (!m_fmodSystem)
		return;

	// Release all sounds
	for (const auto& sound : m_sounds)
	{
		sound.second->sound->release();
		delete sound.second;
	}

	// Close FMOD
	m_result = m_fmodSystem->close();
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return;
	}

	// Release FMOD
	m_result = m_fmodSystem->release();
	if (m_result != FMOD_OK)
		LOG_ERROR(FMOD_ErrorString(m_result));
}

bool Audio::Initialize()
{
	// Create FMOD instance
	m_result = System_Create(&m_fmodSystem);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	// Make sure there is a soundcard devices on the machine
	int driverCount = 0;
	m_result = m_fmodSystem->getNumDrivers(&driverCount);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	// Initialize FMOD
	m_result = m_fmodSystem->init(m_maxChannels, FMOD_INIT_NORMAL, nullptr);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	// Set 3D settings
	m_result = m_fmodSystem->set3DSettings(1.0, m_distanceFactor, 0.0f);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	m_initialized = true;
	return true;
}

void Audio::Update()
{
	if (!m_initialized)
		return;

	// Update FMOD
	m_result = m_fmodSystem->update();
	if (m_result != FMOD_OK)
		LOG_ERROR(FMOD_ErrorString(m_result));
}

bool Audio::CreateSound(const string& filePath)
{
	if (!m_initialized)
		return false;
	
	// Get sound handle
	SoundHandle* soundHandle = new SoundHandle();
	soundHandle->isSound = true;

	// Create sound
	m_result = m_fmodSystem->createSound(filePath.c_str(), FMOD_3D, nullptr, &soundHandle->sound);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	// Set 3D min max disance
	m_result = soundHandle->sound->set3DMinMaxDistance(0.5f * m_distanceFactor, 5000.0f * m_distanceFactor);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	m_sounds.insert(make_pair(filePath, soundHandle));
	return true;
}

bool Audio::CreateStream(const string& filePath)
{
	if (!m_initialized)
		return false;

	// Get sound handle
	SoundHandle* soundHandle = new SoundHandle();
	soundHandle->isSound = false;

	// Create sound
	m_result = m_fmodSystem->createStream(filePath.c_str(), FMOD_3D, nullptr, &soundHandle->sound);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	// Set 3D min max disance
	m_result = soundHandle->sound->set3DMinMaxDistance(0.5f * m_distanceFactor, 5000.0f * m_distanceFactor);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	m_sounds.insert(make_pair(filePath, soundHandle));
	return true;
}

bool Audio::Play(const string& filePath)
{
	if (!m_initialized)
		return false;
	
	// Get sound handle
	SoundHandle* soundHandle = GetSoundByPath(filePath);
	if (!soundHandle)
		return false;

	// Start playing the sound
	m_result = m_fmodSystem->playSound(soundHandle->sound, nullptr, false, &soundHandle->channel);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}	

	return true;
}

bool Audio::Stop(const string& filePath)
{
	if (!m_initialized)
		return false;

	// Get sound handle
	SoundHandle* soundHandle = GetSoundByPath(filePath);
	if (!soundHandle)
		return false;
	
	// Check if the sound is playing
	bool isPlaying;
	m_result = soundHandle->channel->isPlaying(&isPlaying);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}
	
	// If it's playing, then stop it
	if (isPlaying)
		soundHandle->channel->stop();

	return true;
}

bool Audio::SetVolume(float volume, const string& filePath)
{
	SoundHandle* soundHandle = GetSoundByPath(filePath);
	if (!soundHandle)
		return false;

	m_result = soundHandle->channel->setVolume(volume);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool Audio::SetAudioSourceTransform(const string& filePath, Transform* transform)
{
	// Get sound handle
	SoundHandle* soundHandle = GetSoundByPath(filePath);
	if (!soundHandle)
		return false;

	Directus::Math::Vector3 pos = transform->GetPosition();

	FMOD_VECTOR fModPos = { pos.x, pos.y, pos.z };
	FMOD_VECTOR fModVel = { 0, 0, 0 };

	// Set 3D attributes
	m_result = soundHandle->channel->set3DAttributes(&fModPos, &fModVel);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

bool Audio::SetListenerTransform(Transform* transform)
{
	if (!m_initialized || !transform)
		return false;

	// Make sure the listener is not already set
	if (m_listener)
		if (m_listener->GetID() == transform->GetID())
			return true;

	Directus::Math::Vector3 pos = transform->GetPosition();
	Directus::Math::Vector3 forward = transform->GetForward();
	Directus::Math::Vector3 up = transform->GetUp();

	m_pos = { pos.x, pos.y, pos.z };
	m_vel = { 0, 0, 0 };
	m_for = { forward.x, forward.y, forward.z };
	m_up = { up.x, up.y, up.z };

	// Set 3D attributes
	m_result = m_fmodSystem->set3DListenerAttributes(0, &m_pos, &m_vel, &m_for, &m_up);
	if (m_result != FMOD_OK)
	{
		LOG_ERROR(FMOD_ErrorString(m_result));
		return false;
	}

	return true;
}

SoundHandle* Audio::GetSoundByPath(const string& filePath)
{
	for (const auto& sound : m_sounds)
		if (sound.first == filePath)
			return sound.second;

	return nullptr;
}
