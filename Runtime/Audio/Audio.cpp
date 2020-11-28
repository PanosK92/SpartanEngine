/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Spartan.h"
#include "Audio.h"
#include "../Profiling/Profiler.h"
#include "../World/Components/Transform.h"
//========================================

//= NAMESPACES ======
using namespace std;
using namespace FMOD;
//===================

namespace Spartan
{
    Audio::Audio(Context* context) : ISubsystem(context)
    {

    }

    Audio::~Audio()
    {
        // Unsubscribe from events
        UNSUBSCRIBE_FROM_EVENT(EventType::WorldUnload, [this](Variant) { m_listener = nullptr; });

        if (!m_system_fmod)
            return;

        // Close FMOD
        m_result_fmod = m_system_fmod->close();
        if (m_result_fmod != FMOD_OK)
        {
            LogErrorFmod(m_result_fmod);
            return;
        }

        // Release FMOD
        m_result_fmod = m_system_fmod->release();
        if (m_result_fmod != FMOD_OK)
        {
            LogErrorFmod(m_result_fmod);
        }
    }

    bool Audio::Initialize()
    {
        // Create FMOD instance
        m_result_fmod = System_Create(&m_system_fmod);
        if (m_result_fmod != FMOD_OK)
        {
            LogErrorFmod(m_result_fmod);
            return false;
        }

        // Check FMOD version
        uint32_t version;
        m_result_fmod = m_system_fmod->getVersion(&version);
        if (m_result_fmod != FMOD_OK)
        {
            LogErrorFmod(m_result_fmod);
            return false;
        }

        if (version < FMOD_VERSION)
        {
            LogErrorFmod(m_result_fmod);
            return false;
        }

        // Make sure there is a sound card devices on the machine
        auto driver_count = 0;
        m_result_fmod = m_system_fmod->getNumDrivers(&driver_count);
        if (m_result_fmod != FMOD_OK)
        {
            LogErrorFmod(m_result_fmod);
            return false;
        }

        // Initialize FMOD
        m_result_fmod = m_system_fmod->init(m_max_channels, FMOD_INIT_NORMAL, nullptr);
        if (m_result_fmod != FMOD_OK)
        {
            LogErrorFmod(m_result_fmod);
            return false;
        }

        // Set 3D settings
        m_result_fmod = m_system_fmod->set3DSettings(1.0, m_distance_entity, 0.0f);
        if (m_result_fmod != FMOD_OK)
        {
            LogErrorFmod(m_result_fmod);
            return false;
        }

        m_initialized = true;

        // Get version
        stringstream ss;
        ss << hex << version;
        const auto major = ss.str().erase(1, 4);
        const auto minor = ss.str().erase(0, 1).erase(2, 2);
        const auto rev = ss.str().erase(0, 3);
        m_context->GetSubsystem<Settings>()->RegisterThirdPartyLib("FMOD", major + "." + minor + "." + rev, "https://www.fmod.com/download");

        // Get dependencies
        m_profiler = m_context->GetSubsystem<Profiler>();

        // Subscribe to events
        SUBSCRIBE_TO_EVENT(EventType::WorldUnload, [this](Variant) { m_listener = nullptr; });
   
        return true;
    }

    void Audio::Tick(float delta_time)
    {
        // Don't play audio if the engine is not in game mode
        if (!m_context->m_engine->EngineMode_IsSet(Engine_Game))
            return;

        if (!m_initialized)
            return;

        SCOPED_TIME_BLOCK(m_profiler);

        // Update FMOD
        m_result_fmod = m_system_fmod->update();
        if (m_result_fmod != FMOD_OK)
        {
            LogErrorFmod(m_result_fmod);
            return;
        }

        if (m_listener)
        {
            auto position = m_listener->GetPosition();
            auto velocity = Math::Vector3::Zero;
            auto forward = m_listener->GetForward();
            auto up = m_listener->GetUp();

            // Set 3D attributes
            m_result_fmod = m_system_fmod->set3DListenerAttributes(
                0, 
                reinterpret_cast<FMOD_VECTOR*>(&position), 
                reinterpret_cast<FMOD_VECTOR*>(&velocity), 
                reinterpret_cast<FMOD_VECTOR*>(&forward), 
                reinterpret_cast<FMOD_VECTOR*>(&up)
            );
            if (m_result_fmod != FMOD_OK)
            {
                LogErrorFmod(m_result_fmod);
                return;
            }
        }
    }

    void Audio::SetListenerTransform(Transform* transform)
    {
        m_listener = transform;
    }

    void Audio::LogErrorFmod(int error) const
    {
        LOG_ERROR("%s", FMOD_ErrorString(static_cast<FMOD_RESULT>(error)));
    }
}
