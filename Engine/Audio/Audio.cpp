/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include <fmod.hpp>
#include <fmod_errors.h>
#include <sstream>
#include "../Core/Engine.h"
#include "../Core/EventSystem.h"
#include "../Core/Settings.h"
#include "../Profiling/Profiler.h"
#include "../World/Components/Transform.h"
#include "../Core/Context.h"
//========================================

//= NAMESPACES ======
using namespace std;
using namespace FMOD;
//===================

namespace Directus
{
	Audio::Audio(Context* context) : ISubsystem(context)
	{
		m_resultFMOD		= FMOD_OK;
		m_systemFMOD		= nullptr;
		m_maxChannels		= 32;
		m_distanceFentity	= 1.0f;
		m_listener			= nullptr;
		m_profiler			= m_context->GetSubsystem<Profiler>().get();

		// Create FMOD instance
		m_resultFMOD = System_Create(&m_systemFMOD);
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return;
		}

		// Check FMOD version
		unsigned int version;
		m_resultFMOD = m_systemFMOD->getVersion(&version);
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return;
		}

		if (version < FMOD_VERSION)
		{
			LogErrorFMOD(m_resultFMOD);
			return;
		}

		// Make sure there is a sound card devices on the machine
		int driverCount = 0;
		m_resultFMOD = m_systemFMOD->getNumDrivers(&driverCount);
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return;
		}

		// Initialize FMOD
		m_resultFMOD = m_systemFMOD->init(m_maxChannels, FMOD_INIT_NORMAL, nullptr);
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return;
		}

		// Set 3D settings
		m_resultFMOD = m_systemFMOD->set3DSettings(1.0, m_distanceFentity, 0.0f);
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return;
		}

		m_initialized = true;

		// Get version
		stringstream ss;
		ss << hex << version;
		string major	= ss.str().erase(1, 4);
		string minor	= ss.str().erase(0, 1).erase(2, 2);
		string rev		= ss.str().erase(0, 3);
		Settings::Get().m_versionFMOD = major + "." + minor + "." + rev;

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(Event_World_Unload, [this](Variant) { m_listener = nullptr; });
	}

	Audio::~Audio()
	{
		// Unsubscribe from events
		UNSUBSCRIBE_FROM_EVENT(Event_World_Unload, [this](Variant) { m_listener = nullptr; });

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

	void Audio::Tick()
	{
		// Don't play audio if the engine is not in game mode
		if (!Engine::EngineMode_IsSet(Engine_Game))
			return;

		if (!m_initialized)
			return;

		TIME_BLOCK_START_CPU(m_profiler);

		// Update FMOD
		m_resultFMOD = m_systemFMOD->update();
		if (m_resultFMOD != FMOD_OK)
		{
			LogErrorFMOD(m_resultFMOD);
			return;
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
				return;
			}
		}
		//=============================================================

		TIME_BLOCK_END_CPU(m_profiler);
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
