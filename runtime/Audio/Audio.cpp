/*
Copyright(c) 2016-2024 Panos Karabelas

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


//= INCLUDES ======================
#include "pch.h"
#include "Audio.h"
#include "../World/Entity.h"
#include "../Profiling/Profiler.h"
SP_WARNINGS_OFF
#if defined(_MSC_VER)
#include <fmod.hpp>
#include <fmod_errors.h>
#endif
SP_WARNINGS_ON
//=================================

//= NAMESPACES ======
using namespace std;
#if defined(_MSC_VER)
using namespace FMOD;
#endif
//===================

namespace Spartan
{
    namespace
    { 
        #if defined(_MSC_VER)
        FMOD::System* fmod_system  = nullptr;
        uint32_t fmod_result       = 0;
        uint32_t fmod_max_channels = 32;
        float fmod_distance_entity = 1.0f;
        Entity* m_listener         = nullptr;
        #endif
    }

    void Audio::Initialize()
    {
        #if defined(_MSC_VER)
        // Create FMOD instance
        if (!HandleErrorFmod(System_Create(&fmod_system)))
            return;

        // Get FMOD version
        uint32_t version;
        if (!HandleErrorFmod(fmod_system->getVersion(&version)))
            return;

        // Make sure there is a sound device on the machine
        int driver_count = 0;
        if (!HandleErrorFmod(fmod_system->getNumDrivers(&driver_count)))
            return;

        // Initialise FMOD
        if (!HandleErrorFmod(fmod_system->init(fmod_max_channels, FMOD_INIT_NORMAL, nullptr)))
            return;

        // Set 3D settings
        if (!HandleErrorFmod(fmod_system->set3DSettings(1.0, fmod_distance_entity, 0.0f)))
            return;

        // Get version
        stringstream ss;
        ss << hex << version;
        const string major = ss.str().erase(1, 4);
        const string minor = ss.str().erase(0, 1).erase(2, 2);
        const string rev   = ss.str().erase(0, 3);
        Settings::RegisterThirdPartyLib("FMOD", major + "." + minor + "." + rev, "https://www.fmod.com/");

        // Subscribe to events
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldClear, SP_EVENT_HANDLER_EXPRESSION_STATIC
        (
            m_listener = nullptr;
        ));
        #endif 
    }

    void Audio::Shutdown()
    {
        #if defined(_MSC_VER)
        if (!fmod_system)
            return;

        HandleErrorFmod(fmod_system->close());
        HandleErrorFmod(fmod_system->release());
        #endif
    }

    void Audio::Tick()
    {
        #if defined(_MSC_VER)
        // Don't play audio if the engine is not in game mode
        if (!Engine::IsFlagSet(EngineMode::Game))
            return;

        SP_PROFILE_CPU();

        // Update FMOD
        if (!HandleErrorFmod((fmod_system->update())))
            return;

        if (m_listener)
        {
            auto position = m_listener->GetPosition();
            auto velocity = Math::Vector3::Zero;
            auto forward  = m_listener->GetForward();
            auto up       = m_listener->GetUp();

            // Set 3D attributes
            HandleErrorFmod(fmod_system->set3DListenerAttributes(0,
                reinterpret_cast<FMOD_VECTOR*>(&position),
                reinterpret_cast<FMOD_VECTOR*>(&velocity),
                reinterpret_cast<FMOD_VECTOR*>(&forward),
                reinterpret_cast<FMOD_VECTOR*>(&up)
            ));
        }
        #endif
    }

    void Audio::SetListenerEntity(Entity* entity)
    {
        #if defined(_MSC_VER)
        m_listener = entity;
        #endif
    }

    bool Audio::HandleErrorFmod(int result)
    {
        #if defined(_MSC_VER)
        if (result != FMOD_OK)
        {
            SP_LOG_ERROR("%s", FMOD_ErrorString(static_cast<FMOD_RESULT>(result)));
            return false;
        }
        #endif

        return true;
    }

    bool Audio::CreateSound(const string& file_path, int sound_mode, void*& sound)
    {
        #if defined(_MSC_VER)
        return Audio::HandleErrorFmod(fmod_system->createSound(file_path.c_str(), sound_mode, nullptr, reinterpret_cast<FMOD::Sound**>(&sound)));
        #else
        return true;
        #endif
    }

    bool Audio::CreateStream(const string& file_path, int sound_mode, void*& sound)
    {
        #if defined(_MSC_VER)
        return Audio::HandleErrorFmod(fmod_system->createStream(file_path.c_str(), sound_mode, nullptr, reinterpret_cast<FMOD::Sound**>(&sound)));
        #else
        return true;
        #endif
    }

    bool Audio::PlaySound(void* sound, void*& channel)
    {
        if (MUTE == 0)
        {
            #if defined(_MSC_VER)
            return Audio::HandleErrorFmod(fmod_system->playSound(static_cast<FMOD::Sound*>(sound), nullptr, false, reinterpret_cast<FMOD::Channel**>(&channel)));
            #else
            return false;
            #endif
        }
        else
        {
            return false;
        }
    }
}
