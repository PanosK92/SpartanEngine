/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "Engine.h"
#include "EventSystem.h"
#include "Timer.h"
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

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	unsigned int Engine::m_flags = 0;

	Engine::Engine(std::shared_ptr<Context> context)
	{
		m_context = context;

		m_flags |= Engine_Tick;
		m_flags |= Engine_Physics;
		m_flags |= Engine_Game;

		// Initialize global/static subsystems 
		FileSystem::Initialize();
		Settings::Get().Initialize();

		// Register subsystems
		m_context->RegisterSubsystem<Timer>();
		m_context->RegisterSubsystem<Profiler>();
		m_context->RegisterSubsystem<Threading>();	
		m_context->RegisterSubsystem<Input>();
		m_context->RegisterSubsystem<Audio>();
		m_context->RegisterSubsystem<ResourceCache>();
		m_context->RegisterSubsystem<Scripting>();
		m_context->RegisterSubsystem<Physics>();	
		m_context->RegisterSubsystem<World>();	
		m_context->RegisterSubsystem<Renderer>();

		// Initialize above subsystems
		m_context->Initialize();
	}

	Engine::~Engine()
	{
		EventSystem::Get().Clear();
	}

	void Engine::Tick()
	{
		FIRE_EVENT(Event_Frame_Start);

		if (EngineMode_IsSet(Engine_Tick))
		{
			m_context->Tick();
		}

		FIRE_EVENT(Event_Frame_End);
	}
}