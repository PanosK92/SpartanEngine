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
#include "../Multithreading/Multithreading.h"
#include "../Resource/ResourceManager.h"
#include "../Resource/Import/ModelImporter.h"
#include "../Socket/Socket.h"
#include "../Scripting/Scripting.h"
#include "../Graphics/Renderer.h"
#include "../Audio/Audio.h"
//===========================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

Engine::Engine(Context* context) : Subsystem(context)
{
	// Register self as a subsystem
	g_context->RegisterSubsystem(this);

	// Initialize global/static modules 
	Log::Initialize();
	Settings::Initialize();

	// Register subsystems
	g_context->RegisterSubsystem(new Timer(g_context));
	g_context->RegisterSubsystem(new Input(g_context));
	g_context->RegisterSubsystem(new Multithreading(g_context));
	g_context->RegisterSubsystem(new ResourceManager(g_context));
	g_context->RegisterSubsystem(new Graphics(g_context));
	g_context->RegisterSubsystem(new Renderer(g_context));
	g_context->RegisterSubsystem(new Audio(g_context));	
	g_context->RegisterSubsystem(new Physics(g_context));
	g_context->RegisterSubsystem(new Scripting(g_context));
	g_context->RegisterSubsystem(new Scene(g_context));
	g_context->RegisterSubsystem(new Socket(g_context));
}

void Engine::SetHandles(HINSTANCE instance, HWND mainWindowHandle, HWND drawPaneHandle)
{
	m_drawHandle = drawPaneHandle;
	m_windowHandle = mainWindowHandle;
	m_hinstance = instance;
}

bool Engine::Initialize()
{
	// Timer
	if (!g_context->GetSubsystem<Timer>()->Initialize())
	{
		LOG_ERROR("Failed to initialize Timer subsystem");
		return false;
	}

	// Input
	g_context->GetSubsystem<Input>()->SetHandle(m_windowHandle, m_hinstance);
	if (!g_context->GetSubsystem<Input>()->Initialize())
	{
		LOG_ERROR("Failed to initialize Input subsystem");
		return false;
	}

	// Multithreading
	if (!g_context->GetSubsystem<Multithreading>()->Initialize())
	{
		LOG_ERROR("Failed to initialize Multithreading subsystem");
		return false;
	}

	// ResourceManager
	if (!g_context->GetSubsystem<ResourceManager>()->Initialize())
	{
		LOG_ERROR("Failed to initialize ResourceManager subsystem");
		return false;
	}

	// Graphics
	g_context->GetSubsystem<Graphics>()->SetHandle(m_drawHandle);
	if (!g_context->GetSubsystem<Graphics>()->Initialize())
	{
		LOG_ERROR("Failed to initialize Graphics subsystem");
		return false;
	}

	// Renderer
	if (!g_context->GetSubsystem<Renderer>()->Initialize())
	{
		LOG_ERROR("Failed to initialize Renderer subsystem");
		return false;
	}

	// Audio
	if (!g_context->GetSubsystem<Audio>()->Initialize())
	{
		LOG_ERROR("Failed to initialize Audio subsystem");
		return false;
	}

	// Physics
	if (!g_context->GetSubsystem<Physics>()->Initialize())
	{
		LOG_ERROR("Failed to initialize Physics subsystem");
		return false;
	}

	// Scripting
	if (!g_context->GetSubsystem<Scripting>()->Initialize())
	{
		LOG_ERROR("Failed to initialize Scripting subsystem");
		return false;
	}

	// Scene
	if (!g_context->GetSubsystem<Scene>()->Initialize())
	{
		LOG_ERROR("Failed to initialize Scene subsystem");
		return false;
	}

	// Socket
	if (!g_context->GetSubsystem<Socket>()->Initialize())
	{
		LOG_ERROR("Failed to initialize Socket subsystem");
		return false;
	}

	// Temp
	g_context->RegisterSubsystem(new ModelImporter(g_context));

	return true;
}

void Engine::Update()
{
	// This is a full simulation loop
	m_isSimulating = true;

	// TIMER UPDATE
	g_context->GetSubsystem<Timer>()->Update();

	// LOGIC UPDATE
	FIRE_EVENT(EVENT_UPDATE);

	// RENDER UPDATE
	FIRE_EVENT(EVENT_RENDER);
}

void Engine::LightUpdate()
{
	// This is a minimal simulation loop (editor)
	m_isSimulating = false;

	// Manually update as few subsystems as possible
	// This is used by the editor when not in game mode.
	g_context->GetSubsystem<Input>()->Update();
	g_context->GetSubsystem<Scene>()->Update();
	g_context->GetSubsystem<Scene>()->Resolve();
	g_context->GetSubsystem<Renderer>()->Render();
}

void Engine::Shutdown()
{
	// The context will deallocate the subsystems
	// in the reverse order in which they were registered.
	SafeDelete(g_context);

	// Release Log singleton
	Log::Release();
}
