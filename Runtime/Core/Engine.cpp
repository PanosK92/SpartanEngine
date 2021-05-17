/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "Spartan.h"
#include "Window.h"
#include "../Audio/Audio.h"
#include "../Input/Input.h"
#include "../Physics/Physics.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../Scripting/Scripting.h"
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

        // Register subsystems (order matters)
        m_context->AddSubsystem<Settings>();
        m_context->AddSubsystem<Timer>();
        m_context->AddSubsystem<Threading>();
        m_context->AddSubsystem<Window>();
        m_context->AddSubsystem<Input>(TickType::Smoothed);
        m_context->AddSubsystem<ResourceCache>();
        m_context->AddSubsystem<Audio>();
        m_context->AddSubsystem<Physics>();
        m_context->AddSubsystem<Scripting>(TickType::Smoothed);
        m_context->AddSubsystem<World>(TickType::Smoothed);
        m_context->AddSubsystem<Renderer>();
        m_context->AddSubsystem<Profiler>();

        // Initialize above subsystems
        m_context->OnInitialise();
        m_context->OnPreTick();
    }

    Engine::~Engine()
    {
        m_context->OnShutdown();

        // Does this need to become a subsystem ?
        EventSystem::Get().Clear();
    }

    void Engine::Tick() const
    {
        Timer* timer = m_context->GetSubsystem<Timer>();

        m_context->OnTick(TickType::Variable, static_cast<float>(timer->GetDeltaTimeSec()));
        m_context->OnTick(TickType::Smoothed, static_cast<float>(timer->GetDeltaTimeSmoothedSec()));
        m_context->OnPostTick();
    }
}
