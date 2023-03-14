/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "ThreadPool.h"
#include "../Audio/Audio.h"
#include "../Input/Input.h"
#include "../Physics/Physics.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
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
        // Set flags
        SetFlag(EngineMode::Physics);
        SetFlag(EngineMode::Game);

        // Create a context
        m_context = make_shared<Context>();
        m_context->m_engine = this;

        // Add (addition order is tick order)
        Stopwatch timer_add;
        m_context->AddSystem<Window>();
        m_context->AddSystem<Timer>();
        m_context->AddSystem<Input>(TickType::Smoothed);
        m_context->AddSystem<Physics>();
        m_context->AddSystem<World>(TickType::Smoothed);
        m_context->AddSystem<Profiler>();
        m_context->AddSystem<Renderer>();
        SP_LOG_INFO("System addition took %.1f ms", timer_add.GetElapsedTimeMs());

        // Initialise
        Stopwatch timer_initialize;
        {
            // Static
            ThreadPool::Initialize();
            ResourceCache::Initialize(m_context.get());
            Audio::Initialize(m_context.get());

            // Context
            m_context->OnInitialize();
            
        }
        SP_LOG_INFO("System initialization took %.1f ms", timer_initialize.GetElapsedTimeMs());

        // Post initialize
        Stopwatch timer_post_initialize;
        {
            // Context
            m_context->OnPostInitialize();

            // Static
            Settings::PostInitialize(m_context.get());
        }
        SP_LOG_INFO("System post-initialization took %.1f ms", timer_post_initialize.GetElapsedTimeMs());
    }

    Engine::~Engine()
    {
        // Shutdown
        {
            ResourceCache::Clear();

            // Context
            m_context->OnShutdown();

            // Static
            ThreadPool::Shutdown();
            Event::Shutdown();
            Settings::Shutdown();
            Audio::Shutdown();
        }
    }

    void Engine::Tick() const
    {
        // Pre-tick
        m_context->OnPreTick();

        // Tick
        Audio::Tick();
        m_context->OnTick(TickType::Variable, m_context->GetSystem<Timer>()->GetDeltaTimeSec());
        m_context->OnTick(TickType::Smoothed, m_context->GetSystem<Timer>()->GetDeltaTimeSmoothedSec());

        // Post-tick
        m_context->OnPostTick();
    }
}
