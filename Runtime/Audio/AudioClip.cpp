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

//= INCLUDES =============================
#include "AudioClip.h"
#include <fmod.hpp>
#include <fmod_errors.h>
#include "../Logging/Log.h"
#include "../Scene/Components/Transform.h"
#include "Audio.h"
//========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
using namespace FMOD;
//=============================

namespace Directus
{
	AudioClip::AudioClip(Context* context) : IResource(context)
	{
		//= IResource ================
		RegisterResource<AudioClip>();
		//============================

		// AudioClip
		m_transform		= nullptr;
		m_systemFMOD	= (FMOD::System*)context->GetSubsystem<Audio>()->GetSystemFMOD();
		m_result		= FMOD_OK;
		m_soundFMOD		= nullptr;
		m_channelFMOD	= nullptr;
		m_playMode		= Play_Memory;
		m_minDistance	= 1.0f;
		m_maxDistance	= 10000.0f;
		m_modeRolloff	= FMOD_3D_LINEARROLLOFF;
		m_modeLoop		= FMOD_LOOP_OFF;
	}

	AudioClip::~AudioClip()
	{
		if (!m_soundFMOD)
			return;

		m_result = m_soundFMOD->release();
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
		}
	}

	bool AudioClip::LoadFromFile(const std::string& filePath)
	{
		m_soundFMOD = nullptr;
		m_channelFMOD = nullptr;

		return m_playMode == Play_Memory ? CreateSound(filePath) : CreateStream(filePath);
	}

	bool AudioClip::Play()
	{
		// Check if the sound is playing
		if (m_channelFMOD)
		{
			bool isPlaying = false;
			m_result = m_channelFMOD->isPlaying(&isPlaying);
			if (m_result != FMOD_OK)
			{
				LogErrorFMOD(m_result);
				return false;
			}

			// If it's already playing, don't bother
			if (isPlaying)
				return true;
		}

		// Start playing the sound
		m_result = m_systemFMOD->playSound(m_soundFMOD, nullptr, false, &m_channelFMOD);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		return true;
	}

	bool AudioClip::Pause()
	{
		// Check if the sound is playing
		if (m_channelFMOD)
		{
			bool isPaused = false;
			m_result = m_channelFMOD->getPaused(&isPaused);
			if (m_result != FMOD_OK)
			{
				LogErrorFMOD(m_result);
				return false;
			}

			// If it's already stopped, don't bother
			if (!isPaused)
				return true;
		}

		// Stop the sound
		m_result = m_channelFMOD->setPaused(true);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		return true;
	}

	bool AudioClip::Stop()
	{
		if (!m_channelFMOD)
			return true;

		// If it's already stopped, don't bother
		if (!IsPlaying())
			return true;

		// Stop the sound
		m_result = m_channelFMOD->stop();
		if (m_result != FMOD_OK)
		{
			m_channelFMOD = nullptr;
			LogErrorFMOD(m_result); // spams a lot
			return false;
		}

		m_channelFMOD = nullptr;

		return true;
	}

	bool AudioClip::SetLoop(bool loop)
	{
		m_modeLoop = loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF;

		if (!m_soundFMOD)
			return false;

		// Infite loops
		if (loop)
		{
			m_soundFMOD->setLoopCount(-1);
		}

		// Set the channel with the new mode
		m_result = m_soundFMOD->setMode(GetSoundMode());
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		return true;
	}

	bool AudioClip::SetVolume(float volume)
	{
		if (!m_channelFMOD)
			return false;

		m_result = m_channelFMOD->setVolume(volume);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		return true;
	}

	bool AudioClip::SetMute(bool mute)
	{
		if (!m_channelFMOD)
			return false;

		m_result = m_channelFMOD->setMute(mute);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		return true;
	}

	bool AudioClip::SetPriority(int priority)
	{
		if (!m_channelFMOD)
			return false;

		m_result = m_channelFMOD->setPriority(priority);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		return true;
	}

	bool AudioClip::SetPitch(float pitch)
	{
		if (!m_channelFMOD)
			return false;

		m_result = m_channelFMOD->setPitch(pitch);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		return true;
	}

	bool AudioClip::SetPan(float pan)
	{
		if (!m_channelFMOD)
			return false;

		m_result = m_channelFMOD->setPan(pan);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
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
		{
			fmodCurve.push_back(FMOD_VECTOR{ point.x, point.y, point.z });
		}

		m_result = m_channelFMOD->set3DCustomRolloff(&fmodCurve.front(), (int)fmodCurve.size());
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
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
		if (!m_transform || !m_channelFMOD)
			return false;

		Vector3 pos = m_transform->GetPosition();

		FMOD_VECTOR fModPos = { pos.x, pos.y, pos.z };
		FMOD_VECTOR fModVel = { 0, 0, 0 };

		// Set 3D attributes
		m_result = m_channelFMOD->set3DAttributes(&fModPos, &fModVel);
		if (m_result != FMOD_OK)
		{
			m_channelFMOD = nullptr;
			//LogErrorFMOD(m_result); // spams a lot	
			return false;
		}

		return true;
	}

	bool AudioClip::IsPlaying()
	{
		bool isPlaying = false;
		m_result = m_channelFMOD->isPlaying(&isPlaying);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		return isPlaying;
	}

	//= CREATION =====================================================================================
	bool AudioClip::CreateSound(const string& filePath)
	{
		// Create sound
		m_result = m_systemFMOD->createSound(filePath.c_str(), GetSoundMode(), nullptr, &m_soundFMOD);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		// Set 3D min max disance
		m_result = m_soundFMOD->set3DMinMaxDistance(m_minDistance, m_maxDistance);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		return true;
	}

	bool AudioClip::CreateStream(const string& filePath)
	{
		// Create sound
		m_result = m_systemFMOD->createStream(filePath.c_str(), GetSoundMode(), nullptr, &m_soundFMOD);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		// Set 3D min max disance
		m_result = m_soundFMOD->set3DMinMaxDistance(m_minDistance, m_maxDistance);
		if (m_result != FMOD_OK)
		{
			LogErrorFMOD(m_result);
			return false;
		}

		return true;
	}

	int AudioClip::GetSoundMode()
	{
		return FMOD_3D | m_modeLoop | m_modeRolloff;
	}

	void AudioClip::LogErrorFMOD(int error)
	{
		LOG_ERROR("AudioClip::FMOD: " + string(FMOD_ErrorString((FMOD_RESULT)error)));
	}
	//=========================================================================================
}