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
#include "../Display/Display.h"
//===================================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        vector<string> arguments;
        uint32_t flags = 0;

        void write_ci_test_file(const uint32_t value)
        {
            if (Engine::HasArgument("-ci_test"))
            {
                ofstream file("ci_test.txt"); 
                if (file.is_open())
                {
                    file << value;
                    file.close();
                }
            }
        }
    }

    void Engine::Initialize(const vector<string>& args)
    {
        arguments = args;

        SetFlag(EngineMode::Editor, true);
        SetFlag(EngineMode::Physics, true);
        SetFlag(EngineMode::Game, true);

        Stopwatch timer_initialize;
        {
            Log::Initialize();
            FontImporter::Initialize();
            ImageImporterExporter::Initialize();
            ModelImporter::Initialize();
            Window::Initialize();
            Display::Initialize();
            Timer::Initialize();
            Input::Initialize();
            ThreadPool::Initialize();
            ResourceCache::Initialize();
            Audio::Initialize();
            Profiler::Initialize();
            Physics::Initialize();
            Renderer::Initialize();
            World::Initialize();
            Settings::Initialize();
        }

        SP_LOG_INFO("Initialization took %.1f ms", timer_initialize.GetElapsedTimeMs());
        SP_SUBSCRIBE_TO_EVENT(EventType::RendererOnFirstFrameCompleted, SP_EVENT_HANDLER_EXPRESSION_STATIC(write_ci_test_file(0);));
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
        Input::PreTick();

        // tick
        Window::Tick();
        Input::Tick();
        Audio::Tick();
        Physics::Tick();
        World::Tick();
        Renderer::Tick();

        // post-tick
        Timer::PostTick();
        Profiler::PostTick();
    }

    bool Engine::IsFlagSet(const EngineMode flag)
    {
        return flags & static_cast<uint32_t>(flag);
    }

    void Engine::SetFlag(const EngineMode flag, const bool enabled)
    {
        enabled ? (flags |= static_cast<uint32_t>(flag)) : (flags &= ~static_cast<uint32_t>(flag));
    }

    void Engine::ToggleFlag(const EngineMode flag)
    {
        IsFlagSet(flag) ? (flags &= ~static_cast<uint32_t>(flag)) : (flags |= static_cast<uint32_t>(flag));
    }

    bool Engine::HasArgument(const string& argument)
    {
        for (const auto& arg : arguments)
        {
            if (arg == argument)
                return true;
        }

        return false;
    }
}
