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
    static uint32_t m_flags = 0;

    void Engine::Initialize()
    {
        // Set flags
        SetFlag(EngineMode::Physics);
        SetFlag(EngineMode::Game);

        // Initialise
        Stopwatch timer_initialize;
        {
            Window::Initialize();
            Input::Initialize();
            Timer::Initialize();
            ThreadPool::Initialize();
            ResourceCache::Initialize();
            Audio::Initialize();
            Profiler::Initialize();
            Physics::Initialize();
            Renderer::Initialize();
            World::Initialize();
        }
        SP_LOG_INFO("System initialization took %.1f ms", timer_initialize.GetElapsedTimeMs());

        // Post initialize
        Settings::PostInitialize();
    }

    void Engine::Shutdown()
    {
        Renderer::Shutdown();
        World::Shutdown();
        Physics::Shutdown();
        ResourceCache::Clear();
        ThreadPool::Shutdown();
        Event::Shutdown();
        Settings::Shutdown();
        Audio::Shutdown();
        Profiler::Shutdown();
        Window::Shutdown();
    }

    void Engine::Tick()
    {
        // Pre-tick
        Profiler::PreTick();
        World::PreTick();

        // Tick
        Window::Tick();
        Timer::Tick();
        Input::Tick();
        Physics::Tick();
        Audio::Tick();
        World::Tick();
        Renderer::Tick();

        // Post-tick
        Input::PostTick();
        Profiler::PostTick();
    }

    void Engine::SetFlag(const EngineMode flag)
    {
        m_flags |= (1U << static_cast<uint32_t>(flag));
    }

    void Engine::RemoveFlag(const EngineMode flag)
    {
        m_flags &= ~(1U << static_cast<uint32_t>(flag));
    }

    bool Engine::IsFlagSet(const EngineMode flag)
    {
        return m_flags & (1U << static_cast<uint32_t>(flag));
    }

    void Engine::ToggleFlag(const EngineMode flag)
    {
        IsFlagSet(flag) ? RemoveFlag(flag) : SetFlag(flag);
    }
}
