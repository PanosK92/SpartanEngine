/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ===============================
#include "Engine.h"
#include "Scene.h"
#include "Timer.h"
#include "../Logging/Log.h"
#include "../Scripting/ScriptEngine.h"
#include "../Graphics/Renderer.h"
#include "../FileSystem/ModelImporter.h"
#include "../Input/Input.h"
#include "../Graphics/Graphics.h"
#include "../Signals/Signaling.h"
#include "../Multithreading/ThreadPool.h"
#include "../Socket/Socket.h"
#include "../Pools/ShaderPool.h"
#include "../Pools/MaterialPool.h"
#include "../Pools/TexturePool.h"
//==========================================

//= NAMESPACES =====
using namespace std;
//==================

Engine::Engine(Context* context) : Object(context) 
{
	// Register self as a subsystem
	g_context->RegisterSubsystem(this);

	// Initialize Singletons
	Log::Initialize();

	// Register subsystems that don't depend on any startup parameters
	g_context->RegisterSubsystem(new Timer(g_context));
	g_context->RegisterSubsystem(new ThreadPool(g_context));
	g_context->RegisterSubsystem(new Graphics(g_context));
	g_context->RegisterSubsystem(new Input(g_context));
	g_context->RegisterSubsystem(new PhysicsWorld(g_context));
	g_context->RegisterSubsystem(new MeshPool(g_context));
	g_context->RegisterSubsystem(new TexturePool(g_context));
}

Engine::~Engine()
{

}

void Engine::Initialize(HINSTANCE instance, HWND windowHandle, HWND drawPaneHandle)
{
	// Initialize subsystems that depend on external parameters
	g_context->GetSubsystem<Graphics>()->Initialize(drawPaneHandle);
	g_context->GetSubsystem<Input>()->Initialize(instance, windowHandle);

	// Register subsystems which depend on registered subsystems
	g_context->RegisterSubsystem(new ScriptEngine(g_context));
	g_context->RegisterSubsystem(new ShaderPool(g_context));
	g_context->RegisterSubsystem(new MaterialPool(g_context));
	g_context->RegisterSubsystem(new ModelImporter(g_context)); 
	g_context->RegisterSubsystem(new Renderer(g_context));
	g_context->RegisterSubsystem(new Scene(g_context));
	g_context->RegisterSubsystem(new Socket(g_context));

	// Finally, initialize the scene (add a camera, a skybox and so on)
	g_context->GetSubsystem<Scene>()->Initialize();
}

void Engine::Update()
{
	// Get subsystems
	Timer* timer = g_context->GetSubsystem<Timer>();

	timer->Update();
	g_context->GetSubsystem<Input>()->Update();

	//= PHYSICS ==================================
	g_context->GetSubsystem<PhysicsWorld>()->Step(timer->GetDeltaTime());
	//============================================

	//= SCENE ====================================	
	g_context->GetSubsystem<Scene>()->Resolve();
	//============================================

	//= RENDERING ================================
	timer->RenderStart();
	g_context->GetSubsystem<Renderer>()->Render();
	timer->RenderEnd();
	//============================================
}

Context* Engine::GetContext()
{
	return g_context;
}

void Engine::Shutdown()
{
	// The context will deallocate the subsystems
	// in the reverse order in which they were registered.
	delete g_context;

	// Release singletons
	Log::Release();
}
