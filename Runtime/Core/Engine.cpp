/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES =================================
#include "Engine.h"
#include "Timer.h"
#include "Settings.h"
#include "../Logging/Log.h"
#include "../Threading/Threading.h"
#include "../Resource/ResourceManager.h"
#include "../Socket/Socket.h"
#include "../Scripting/Scripting.h"
#include "../Graphics/Renderer.h"
#include "../Audio/Audio.h"
#include "../Graphics/GraphicsDefinitions.h"
#include "../EventSystem/EventSystem.h"
#include "../Input/Input.h"
#include "../Physics/Physics.h"
//===========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	Engine::Engine(Context* context) : Subsystem(context)
	{
		// Register self as a subsystem
		m_context->RegisterSubsystem(this);

		// Initialize global/static modules 
		Log::Initialize();
		FileSystem::Initialize();
		Settings::Initialize();

		// Register subsystems
		m_context->RegisterSubsystem(new Timer(m_context));
		m_context->RegisterSubsystem(new Input(m_context));
		m_context->RegisterSubsystem(new Threading(m_context));
		m_context->RegisterSubsystem(new ResourceManager(m_context));
		m_context->RegisterSubsystem(new Graphics(m_context));
		m_context->RegisterSubsystem(new Renderer(m_context));
		m_context->RegisterSubsystem(new Audio(m_context));
		m_context->RegisterSubsystem(new Physics(m_context));
		m_context->RegisterSubsystem(new Scripting(m_context));
		m_context->RegisterSubsystem(new Scene(m_context));
		m_context->RegisterSubsystem(new Socket(m_context));
	}

	void Engine::SetHandles(void* instance, void* mainWindowHandle, void* drawPaneHandle)
	{
		m_drawHandle = drawPaneHandle;
		m_windowHandle = mainWindowHandle;
		m_hinstance = instance;
	}

	bool Engine::Initialize()
	{
		// Timer
		if (!m_context->GetSubsystem<Timer>()->Initialize())
		{
			LOG_ERROR("Failed to initialize Timer subsystem");
			return false;
		}

		// Input
		m_context->GetSubsystem<Input>()->SetHandle((HWND)m_windowHandle, (HINSTANCE)m_hinstance);
		if (!m_context->GetSubsystem<Input>()->Initialize())
		{
			LOG_ERROR("Failed to initialize Input subsystem");
			return false;
		}

		// Multithreading
		if (!m_context->GetSubsystem<Threading>()->Initialize())
		{
			LOG_ERROR("Failed to initialize Multithreading subsystem");
			return false;
		}

		// ResourceManager
		if (!m_context->GetSubsystem<ResourceManager>()->Initialize())
		{
			LOG_ERROR("Failed to initialize ResourceManager subsystem");
			return false;
		}

		// Graphics
		m_context->GetSubsystem<Graphics>()->SetHandle(m_drawHandle);
		if (!m_context->GetSubsystem<Graphics>()->Initialize())
		{
			LOG_ERROR("Failed to initialize Graphics subsystem");
			return false;
		}

		// Renderer
		if (!m_context->GetSubsystem<Renderer>()->Initialize())
		{
			LOG_ERROR("Failed to initialize Renderer subsystem");
			return false;
		}

		// Audio
		if (!m_context->GetSubsystem<Audio>()->Initialize())
		{
			LOG_ERROR("Failed to initialize Audio subsystem");
			return false;
		}

		// Physics
		if (!m_context->GetSubsystem<Physics>()->Initialize())
		{
			LOG_ERROR("Failed to initialize Physics subsystem");
			return false;
		}

		// Scripting
		if (!m_context->GetSubsystem<Scripting>()->Initialize())
		{
			LOG_ERROR("Failed to initialize Scripting subsystem");
			return false;
		}

		// Scene
		if (!m_context->GetSubsystem<Scene>()->Initialize())
		{
			LOG_ERROR("Failed to initialize Scene subsystem");
			return false;
		}

		// Socket
		if (!m_context->GetSubsystem<Socket>()->Initialize())
		{
			LOG_ERROR("Failed to initialize Socket subsystem");
			return false;
		}

		LOG_INFO("Initialized successfully");
		return true;
	}

	void Engine::Update()
	{
		// TIMER UPDATE
		m_context->GetSubsystem<Timer>()->Update();

		// LOGIC UPDATE
		FIRE_EVENT(EVENT_UPDATE);

		// RENDER UPDATE
		FIRE_EVENT(EVENT_RENDER);
	}

	void Engine::Shutdown()
	{
		// The context will deallocate the subsystems
		// in the reverse order in which they were registered.
		SafeDelete(m_context);

		// Release Log singleton
		Log::Release();
	}
}