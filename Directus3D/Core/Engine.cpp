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
#include "Socket.h"
#include "Scene.h"
#include "Timer.h"
#include "../IO/Log.h"
#include "../Pools/GameObjectPool.h"
#include "../Scripting/ScriptEngine.h"
#include "../Graphics/Renderer.h"
#include "../AssetImporting/ModelImporter.h"
#include "../Input/Input.h"
#include "../Graphics/Graphics.h"
#include "../Signals/Signaling.h"
#include "../Multithreading/ThreadPool.h"
//==========================================

//= NAMESPACES =====
using namespace std;
//==================

Engine::Engine(Context* context) : Object(context) 
{
	m_context = context;
}

Engine::~Engine()
{

}

void Engine::Initialize(HINSTANCE instance, HWND windowHandle, HWND drawPaneHandle)
{
	EMIT_SIGNAL(SIGNAL_ENGINE_INITIALIZE);

	// Create a new context
	m_context = new Context();

	// Register self as a subsystem
	m_context->RegisterSubsystem(this);

	// Initialize Singletons
	Log::Initialize();
	ImageImporter::GetInstance();

	// Register subsystems that don't depend on any startup parameters
	m_context->RegisterSubsystem(new Timer(m_context));
	m_context->RegisterSubsystem(new ThreadPool(m_context));
	m_context->RegisterSubsystem(new Graphics(m_context));
	m_context->RegisterSubsystem(new Input(m_context));
	m_context->RegisterSubsystem(new PhysicsWorld(m_context));
	m_context->RegisterSubsystem(new MeshPool(m_context));
	m_context->RegisterSubsystem(new TexturePool(m_context));

	// Initialize subsystems that depend on external parameters
	m_context->GetSubsystem<Graphics>()->Initialize(drawPaneHandle);
	m_context->GetSubsystem<Input>()->Initialize(instance, windowHandle);

	// Register subsystems dependent subsystems
	m_context->RegisterSubsystem(new ScriptEngine(m_context)); // Depends on Input, Timer
	m_context->RegisterSubsystem(new ShaderPool(m_context)); // Depends on Graphics
	m_context->RegisterSubsystem(new MaterialPool(m_context));  // Depends on TexturePool, ShaderPool
	m_context->RegisterSubsystem(new ModelImporter(m_context));  // Depends on MeshPool, TexturePool, ShaderPool, MaterialPool, ThreadPool
	m_context->RegisterSubsystem(new Renderer(m_context)); // Depends on Graphics, Timer, PhysicsWorld, ShaderPool, MaterialPool
	m_context->RegisterSubsystem(new Scene(m_context)); // Depends on MeshPool, TexturePool, ShaderPool, MaterialPool, ThreadPool, PhysicsWorld, Renderer
	m_context->RegisterSubsystem(new Socket(m_context)); // Depends on everything (potentially)

	GameObjectPool::GetInstance().Initialize(m_context);

	// Resolve the scene
	m_context->GetSubsystem<Scene>()->Resolve();
}

void Engine::Update()
{
	EMIT_SIGNAL(SIGNAL_FRAME_START);

	// Get subsystems
	Input* input = m_context->GetSubsystem<Input>();
	Timer* timer = m_context->GetSubsystem<Timer>();
	Renderer* renderer = m_context->GetSubsystem<Renderer>();
	Scene* scene = m_context->GetSubsystem<Scene>();
	PhysicsWorld* physicsWorld = m_context->GetSubsystem<PhysicsWorld>();

	timer->Update();
	input->Update();

	//= PHYSICS ==================================
	physicsWorld->Step(timer->GetDeltaTime());
	//============================================

	//= SCENE ====================================	
	GameObjectPool::GetInstance().Update();
	scene->Resolve();
	//============================================

	//= RENDERING ================================
	timer->RenderStart();
	renderer->Render();
	timer->RenderEnd();
	//============================================

	EMIT_SIGNAL(SIGNAL_FRAME_END);
}

void Engine::Shutdown()
{
	EMIT_SIGNAL(SIGNAL_ENGINE_SHUTDOWN);

	// 15 - GAMEOBJECT POOL
	GameObjectPool::GetInstance().Release();

	// 10 - IMAGE LOADER
	ImageImporter::GetInstance().Clear();

	// 1 - LOG
	Log::Release();
}