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

//= INCLUDES =========================
#include "Spartan.h"
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
    Engine::Engine(const WindowData& window_data)
    {
        // Window
        m_window_data = window_data;

        // Flags
        m_flags |= Engine_Physics;
        m_flags |= Engine_Game;

        // Create context
        m_context = make_shared<Context>();
        m_context->m_engine = this;

        // Register subsystems
        m_context->RegisterSubsystem<Timer>(TickType::Variable);         // must be first so it ticks first
        m_context->RegisterSubsystem<Threading>(TickType::Variable);
        m_context->RegisterSubsystem<ResourceCache>(TickType::Variable);
        m_context->RegisterSubsystem<Audio>(TickType::Variable);
        m_context->RegisterSubsystem<Physics>(TickType::Variable);       // integrates internally
        m_context->RegisterSubsystem<Input>(TickType::Smoothed);
        m_context->RegisterSubsystem<Scripting>(TickType::Smoothed);
        m_context->RegisterSubsystem<World>(TickType::Smoothed);
        m_context->RegisterSubsystem<Profiler>(TickType::Variable);
        m_context->RegisterSubsystem<Renderer>(TickType::Smoothed);
        m_context->RegisterSubsystem<Settings>(TickType::Variable);
                 
        // Initialize above subsystems
        m_context->Initialize();

        m_timer = m_context->GetSubsystem<Timer>();
    }

    Engine::~Engine()
    {
        EventSystem::Get().Clear(); // this must become a subsystem
    }

    void Engine::Tick() const
    {
        m_context->Tick(TickType::Variable, static_cast<float>(m_timer->GetDeltaTimeSec()));
        m_context->Tick(TickType::Smoothed, static_cast<float>(m_timer->GetDeltaTimeSmoothedSec()));
    }

    void Engine::SetWindowData(WindowData& window_data)
    {
        m_window_data = window_data;
        FIRE_EVENT(EventType::WindowData);
    }
}
