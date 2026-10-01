/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "Runtime.h"
#include "core/Engine.h"
#include "core/Window.h"
#include "core/Timer.h"
#ifdef SP_GAME
#include "game/GameWorld.h"
#endif
#include "world/World.h"
#include "rendering/Renderer.h"
#include "profiling/Profiler.h"
#include "ui/ImGui.h"

namespace spartan
{
    int RunRuntime(const std::vector<std::string>& args)
    {
        // A runtime launches an explicit world; paths are relative to the executable.
        std::string world;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (args[i] == "--world" && i + 1 < args.size()) world = args[++i];
            else if (FileSystem::GetExtensionFromFilePath(args[i]) == ".world") world = args[i];
        }
        if (world.empty() || !FileSystem::Exists(world)) return 1;
        Engine::Initialize(args);
#ifdef SP_GAME
        game::Initialize();
#endif
        gui::initialize(false);
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldLoaded, [](sp_variant) { Engine::SetFlag(EngineMode::Playing, true); });
        World::LoadFromFile(world);
        Timer::Reset();
        while (!Window::WantsToClose())
        {
            Profiler::FrameStart();
            Profiler::TimeBlockStart("frame_active", TimeBlockType::Cpu);
            World::ProcessPendingLoad();
            Renderer::SetPresentInRenderer(false);
            gui::begin_frame();
            Engine::Tick();
            gui::render();
            Profiler::TimeBlockEnd(TimeBlockType::Cpu);
            Timer::PostTick();
            Profiler::PostTick();
        }
        gui::shutdown();
        Engine::Shutdown();
        return 0;
    }
}
