/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES =========================
#include "pch.h"
#include "Window.h"
#include "../Audio/Audio.h"
#include "../Input/Input.h"
#include "../Physics/Physics.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../Threading/Threading.h"
#include "../World/World.h"
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    Engine::Engine()
    {
        // Flags
        m_flags |= Engine_Physics;
        m_flags |= Engine_Game;

        // Create context
        m_context = make_shared<Context>();
        m_context->m_engine = this;

        // Subsystem: Add (this is also the ticking order)
        Stopwatch timer_add;
        m_context->AddSubsystem<Window>();
        m_context->AddSubsystem<Settings>();
        m_context->AddSubsystem<Timer>();
        m_context->AddSubsystem<Threading>();
        m_context->AddSubsystem<Input>(TickType::Smoothed);
        m_context->AddSubsystem<ResourceCache>();
        m_context->AddSubsystem<Audio>();
        m_context->AddSubsystem<Physics>();
        m_context->AddSubsystem<World>(TickType::Smoothed);
        m_context->AddSubsystem<Profiler>();
        m_context->AddSubsystem<Renderer>();
        LOG_INFO("Subsystem addition took %.1f ms", timer_add.GetElapsedTimeMs());

        // Subsystem: Initialise.
        Stopwatch timer_initialise;
        m_context->OnInitialise();
        LOG_INFO("Subsystem initialisation took %.1f ms", timer_initialise.GetElapsedTimeMs());

        // Subsystem: Post-initialise.
        Stopwatch timer_post_initialise;
        m_context->OnPostInitialise();
        LOG_INFO("Subsystem post-initialisation took %.1f ms", timer_post_initialise.GetElapsedTimeMs());
    }

    Engine::~Engine()
    {
        // Subsystem: Shutdown.
        m_context->OnShutdown();

        // Does this need to become a subsystem ?
        EventSystem::Get().Clear();
    }

    void Engine::Tick() const
    {
        // Subsystem: Pre-tick.
        m_context->OnPreTick();

        // Subsystem: Tick.
        m_context->OnTick(TickType::Variable, m_context->GetSubsystem<Timer>()->GetDeltaTimeSec());
        m_context->OnTick(TickType::Smoothed, m_context->GetSubsystem<Timer>()->GetDeltaTimeSmoothedSec());

        // Subsystem: Post-tick.
        m_context->OnPostTick();
    }
}
