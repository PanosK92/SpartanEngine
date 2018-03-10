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
#include "Audio.h"
#include "fmod.hpp"
#include "fmod_errors.h"
#include "../Logging/Log.h"
#include "../EventSystem/EventSystem.h"
#include <sstream>
#include "../Core/Settings.h"
#include "../Scene/Components/Transform.h"
//========================================

//= NAMESPACES ======
using namespace std;
using namespace FMOD;
//===================

namespace Directus
{
	Audio::Audio(Context* context) : Subsystem(context)
	{
		m_resultFMOD		= FMOD_OK;
		m_systemFMOD		= nullptr;
		m_maxChannels		= 32;
		m_distanceFactor	= 1.0f;
		m_initialized		= false;
		m_listener			= nullptr;

		// Subscribe to update event
		SUBSCRIBE_TO_EVENT(EVENT_UPDATE, EVENT_HANDLER(Update));
	}

	Audio::~Audio()
	{
		if (!m_systemFMOD)
			return;

		// Close FMOD
		m_resultFMOD = m_systemFMOD->close();
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return;
		}

		// Release FMOD
		m_resultFMOD = m_systemFMOD->release();
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
		}
	}

	bool Audio::Initialize()
	{
		// Create FMOD instance
		m_resultFMOD = System_Create(&m_systemFMOD);
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return false;
		}

		// Check FMOD version
		unsigned int version;
		m_resultFMOD = m_systemFMOD->getVersion(&version);
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return false;
		}

		if (version < FMOD_VERSION)
		{
			LogErrorFMOD(m_resultFMOD);
			return false;
		}

		// Make sure there is a sound card devices on the machine
		int driverCount = 0;
		m_resultFMOD = m_systemFMOD->getNumDrivers(&driverCount);
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return false;
		}

		// Initialize FMOD
		m_resultFMOD = m_systemFMOD->init(m_maxChannels, FMOD_INIT_NORMAL, nullptr);
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return false;
		}

		// Set 3D settings
		m_resultFMOD = m_systemFMOD->set3DSettings(1.0, m_distanceFactor, 0.0f);
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return false;
		}

		// Log version
		stringstream ss;
		ss << hex << version;
		string major	= ss.str().erase(1, 4);
		string minor	= ss.str().erase(0, 1).erase(2, 2);
		string rev		= ss.str().erase(0, 3);
		Settings::Get().g_versionFMOD = major + "." + minor + "." + rev;
		LOG_INFO("Audio: FMOD " + Settings::Get().g_versionFMOD);

		m_initialized = true;
		return true;
	}

	bool Audio::Update()
	{
		if (!m_initialized)
			return false;

		// Update FMOD
		m_resultFMOD = m_systemFMOD->update();
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return false;
		}

		//= 3D Attributes =============================================
		if (m_listener)
		{
			Math::Vector3 position = m_listener->GetPosition();
			Math::Vector3 velocity = Math::Vector3::Zero;
			Math::Vector3 forward = m_listener->GetForward();
			Math::Vector3 up = m_listener->GetUp();

			// Set 3D attributes
			m_resultFMOD = m_systemFMOD->set3DListenerAttributes(
				0, 
				(FMOD_VECTOR*)&position, 
				(FMOD_VECTOR*)&velocity, 
				(FMOD_VECTOR*)&forward, 
				(FMOD_VECTOR*)&up
			);
			if (m_resultFMOD != FMOD_OK)
			{
				LogErrorFMOD(m_resultFMOD);
				return false;
			}
		}
		//=============================================================

		return true;
	}

	void Audio::SetListenerTransform(Transform* transform)
	{
		m_listener = transform;
	}

	void Audio::LogErrorFMOD(int error)
	{
		LOG_ERROR("Audio::FMOD: " + string(FMOD_ErrorString((FMOD_RESULT)error)));
	}
}
