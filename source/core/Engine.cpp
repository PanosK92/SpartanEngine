/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ================================
#include "pch.h"
#include "Window.h"
#include "ThreadPool.h"
#include "../input/Input.h"
#include "../world/World.h"
#include "../physics/PhysicsWorld.h"
#include "../profiling/Profiler.h"
#include "../rendering/Renderer.h"
#include "../resource/ResourceCache.h"
#include "../resource/import/FontImporter.h"
#include "../resource/import/ModelImporter.h"
#include "../resource/import/ImageImporter.h"
#include "../display/Display.h"
#include "../memory/Allocator.h"
#include "../testing/SmokeTest.h"
#include "../rhi/RHI_Device.h"
#include "../xr/Xr.h"
#include "../commands/console/ConsoleCommands.h"
#ifndef SP_RUNTIME
#include "../mcp/McpServer.h"
#endif
#include "../steam/Steam.h"
#include "../resource/IconAtlas.h"
#include "Settings.h"
#include <future>
//===========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        vector<string> arguments;
        uint32_t flags = 0;
    }

    void Engine::Initialize(const vector<string>& args)
    {
        arguments = args;

#ifdef SP_RUNTIME
        SetFlag(EngineMode::EditorVisible, false);
#else
        SetFlag(EngineMode::EditorVisible, !HasArgument("-game"));
#endif
        SetFlag(EngineMode::Playing,       true);

        // initialize
        Stopwatch timer_initialize;
        {
            Log::Initialize();
            if (IsStartupSmokeTest())
            {
                SP_LOG_INFO("Startup smoke test: initializing CPU subsystems (no window, GPU, scene or network services)");
                SetFlag(EngineMode::Playing, false);
                FontImporter::Initialize();
                ImageImporter::Initialize();
                Timer::Initialize();
                ThreadPool::Initialize();
                ResourceCache::Initialize();
                Profiler::Initialize();
                ThreadPool::AddTask([] { PhysicsWorld::Initialize(); }).get();
                World::Initialize();
                SP_LOG_INFO("Startup smoke test: engine initialized");
                return;
            }
            Settings::LoadPreInitSettings();
            FontImporter::Initialize();
            ImageImporter::Initialize();
            Window::Initialize();
            Display::Initialize();
            Timer::Initialize();
            Input::Initialize();
            ThreadPool::Initialize();
            ResourceCache::Initialize();
            Profiler::Initialize();

            // overlap independent cpu work with the heavy renderer path
            future<void> physics_future = ThreadPool::AddTask([]()
            {
                PhysicsWorld::Initialize();
            });
            future<void> icon_decode_future = ThreadPool::AddTask([]()
            {
                IconAtlas::DecodeSources();
            });

            Renderer::Initialize();
            Window::PumpEvents();
            World::Initialize();
            Settings::Initialize();
            SmokeTest::Initialize();
#ifndef SP_RUNTIME
            McpServer::Initialize(args);
#endif
            if (!HasArgument("--no-steam") && !IsHeadless())
            {
                Steam::Initialize(); // must stay on the main thread, steam callbacks run here too
            }

            physics_future.get();
            icon_decode_future.get();

            // xr is intentionally not auto initialized here, ctrl+0 brings it up on demand
            // so the openxr runtime (e.g. steamvr) is never spawned without explicit intent
        }

        // post-initialize
        {
            // gpu-capability defaults for new users (no settings file)
            // existing users keep their saved preferences
            if (!Settings::HasLoadedUserSettingsFromFile())
            {
                bool ray_tracing_supported = RHI_Device::IsSupportedRayTracing();
                ConsoleRegistry::Get().SetValueFromString("r.ray_traced_reflections", std::to_string(static_cast<float>(ray_tracing_supported)));
                ConsoleRegistry::Get().SetValueFromString("r.ray_traced_shadows", std::to_string(static_cast<float>(ray_tracing_supported)));
                ConsoleRegistry::Get().SetValueFromString("r.mesh_shaders", std::to_string(static_cast<float>(RHI_Device::IsSupportedMeshShaders())));

                Renderer_AntiAliasing_Upsampling aa = Renderer_AntiAliasing_Upsampling::AA_Taau_Upscale_Taau;
                if (RHI_Device::IsSupportedDlss())
                {
                    aa = Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss;
                }
                ConsoleRegistry::Get().SetValueFromString("r.antialiasing_upsampling", std::to_string(static_cast<float>(aa)));
            }

            Window::PumpEvents();
            ResourceCache::LoadDefaultResources();
            Window::PumpEvents();
        }

        SP_LOG_INFO("%s has been initialized. Duration %.1f sec", version::c_str(), timer_initialize.GetElapsedTimeSec());
    }

    void Engine::Shutdown()
    {
        if (IsStartupSmokeTest())
        {
            SP_LOG_INFO("Startup smoke test: shutting down");
            ThreadPool::Flush();
            World::Shutdown();
            ThreadPool::Shutdown();
            PhysicsWorld::Shutdown();
            Profiler::Shutdown();
            Event::Shutdown();
            ImageImporter::Shutdown();
            FontImporter::Shutdown();
            return;
        }
        Steam::Shutdown();
#ifndef SP_RUNTIME
        McpServer::Shutdown();
#endif
        Profiler::Shutdown();

        // Join existing work, then keep workers available while entity Stop callbacks finish.
        ThreadPool::Flush();

        // world must tear down first, DestroyAccelerationStructures and entity
        // destructors still need live meshes and materials from the resource cache
        World::Shutdown();
        ThreadPool::Shutdown();
        ResourceCache::UnloadDefaultResources();

        PhysicsWorld::Shutdown();
        Xr::Shutdown();
        Renderer::Shutdown();
   
        Event::Shutdown();
        Window::Shutdown();
        ImageImporter::Shutdown();
        FontImporter::Shutdown();
        Settings::Shutdown();
    }

    void Engine::Tick()
    {
        if (IsStartupSmokeTest())
        {
            PhysicsWorld::Tick();
            World::Tick();
            SP_FIRE_EVENT(EventType::WorldTicked);
            Allocator::Tick();
            return;
        }
        // pre-tick
        Input::PreTick();
#ifndef SP_RUNTIME
        McpServer::Tick();
#endif
        Steam::Tick();

        // ctrl+0 toggles openxr for whatever runtime/headset is active (steamvr, psvr2 via stvr, etc)
        if ((Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right)) && Input::GetKeyDown(KeyCode::Alpha0))
        {
            if (!Xr::IsAvailable())
            {
                SP_LOG_INFO("openxr: enabling (any connected openxr headset)");
                Xr::Initialize();
            }
            else
            {
                SP_LOG_INFO("openxr: disabling");
                Xr::Shutdown();
            }
        }

        // f12 takes a screenshot, usable with the headset on
        if (Input::GetKeyDown(KeyCode::F12))
        {
            Renderer::Screenshot();
        }

        // tick
        Window::Tick();
        Input::Tick();
        SP_FIRE_EVENT(EventType::InputTicked);
        PhysicsWorld::Tick();
        World::Tick();
        SP_FIRE_EVENT(EventType::WorldTicked);
        PhysicsWorld::DrawDebugVisualization();
        Xr::Tick();
        Renderer::Tick();
        Allocator::Tick();
        SmokeTest::Tick();

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
            {
                return true;
            }
        }

        return false;
    }

    bool Engine::IsHeadless()
    {
        return IsStartupSmokeTest() || HasArgument("--headless") || HasArgument("-headless") || (HasArgument("--mcp-control") && HasArgument("--mcp-hidden"));
    }

    bool Engine::IsStartupSmokeTest()
    {
        return HasArgument("--ci-smoke-test");
    }
}
