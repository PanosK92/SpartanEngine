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

//= INCLUDES ========================================
#include "pch.h"
#include "Window.h"
#include "ThreadPool.h"
#include "../Audio/Audio.h"
#include "../Input/Input.h"
#include "../World/World.h"
#include "../Physics/Physics.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/Import/FontImporter.h"
#include "../Resource/Import/ModelImporter.h"
#include "../Resource/Import/ImageImporterExporter.h"
//===================================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        uint32_t flags = 0;
    }

    void Engine::Initialize()
    {
        AddFlag(EngineMode::Editor);
        AddFlag(EngineMode::Physics);
        AddFlag(EngineMode::Game);

        // initialize systems
        Stopwatch timer_initialize;
        {
            Log::Initialize();
            Settings::Initialize();
            FontImporter::Initialize();
            ImageImporterExporter::Initialize();
            ModelImporter::Initialize();
            Window::Initialize();
            Timer::Initialize();
            Input::Initialize();
            ThreadPool::Initialize();
            ResourceCache::Initialize();
            Audio::Initialize();
            Profiler::Initialize();
            Physics::Initialize();
            Renderer::Initialize();
            World::Initialize();

            // post
            Settings::PostInitialize();
        }

        SP_LOG_INFO("Initialization took %.1f ms", timer_initialize.GetElapsedTimeMs());
    }

    void Engine::Shutdown()
    {
        SP_FIRE_EVENT(EventType::EngineShutdown);

        ResourceCache::Shutdown();
        World::Shutdown();
        Renderer::Shutdown();
        Physics::Shutdown();
        ThreadPool::Shutdown();
        Event::Shutdown();
        Audio::Shutdown();
        Profiler::Shutdown();
        Window::Shutdown();
        ImageImporterExporter::Shutdown();
        FontImporter::Shutdown();
        Settings::Shutdown();
    }

    void Engine::Tick()
    {
        // pre-tick
        Profiler::PreTick();

        // tick
        Window::Tick();
        Input::Tick();
        Audio::Tick();
        Physics::Tick();
        World::Tick();
        Renderer::Tick();

        // post-tick
        Input::PostTick();
        Timer::PostTick();
        Profiler::PostTick();
        Renderer::PostTick();
    }

    void Engine::AddFlag(const EngineMode flag)
    {
        flags |= static_cast<uint32_t>(flag);
    }

    void Engine::RemoveFlag(const EngineMode flag)
    {
        flags &= ~static_cast<uint32_t>(flag);
    }

    bool Engine::IsFlagSet(const EngineMode flag)
    {
        return flags & static_cast<uint32_t>(flag);
    }

    void Engine::ToggleFlag(const EngineMode flag)
    {
        IsFlagSet(flag) ? RemoveFlag(flag) : AddFlag(flag);
    }
}
