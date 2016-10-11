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

//= INCLUDES ==========================
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
#include "Helper.h"
#include "../Signals/Signaling.h"
#include "../Pools/ThreadPool.h"
//=====================================

Engine::Engine()
{
	m_engineSocket = nullptr;
	m_scriptEngine = nullptr;
	m_renderer = nullptr;
	m_modelLoader = nullptr;
	m_scene = nullptr;
	m_input = nullptr;
	m_timer = nullptr;
	m_physicsWorld = nullptr;
	m_meshPool = nullptr;
	m_materialPool = nullptr;
	m_texturePool = nullptr;
	m_graphics = nullptr;
	m_shaderPool = nullptr;
	m_threadPool = nullptr;
}

Engine::~Engine()
{

}

void Engine::Initialize(HINSTANCE instance, HWND windowHandle, HWND drawPaneHandle)
{
	EMIT_SIGNAL(SIGNAL_ENGINE_INITIALIZE);

	// 1 - LOG
	Log::Initialize();

	// 2 - TIMER
	m_timer = new Timer();
	m_timer->Initialize();

	// 3 - THREAD POOL
	m_threadPool = new ThreadPool();

	// 4 - GRAPHICS
	m_graphics = new Graphics();
	m_graphics->Initialize(drawPaneHandle);

	// 5 - INPUT
	m_input = new Input();
	m_input->Initialize(instance, windowHandle);

	// 6 - PHYSICS ENGINE
	m_physicsWorld = new PhysicsWorld();
	m_physicsWorld->Initialize();

	// 7 - SCRIPT ENGINE
	m_scriptEngine = new ScriptEngine(m_timer, m_input);
	m_scriptEngine->Initialize();

	// 8 - SHADER POOL
	m_shaderPool = new ShaderPool(m_graphics);

	// 9 - MESH POOL
	m_meshPool = new MeshPool();

	// 10 - IMAGE LOADER
	ImageImporter::GetInstance().Initialize(m_graphics);

	// 11 - TEXTURE POOL
	m_texturePool = new TexturePool();

	// 12 - MATERIAL POOL
	m_materialPool = new MaterialPool(m_texturePool, m_shaderPool);

	// 13 - MODEL LOADER
	m_modelLoader = new ModelImporter();
	m_modelLoader->Initialize(m_meshPool, m_texturePool, m_shaderPool, m_materialPool);

	// 14 - RENDERER
	m_renderer = new Renderer();
	m_scene = new Scene(m_texturePool, 
		m_materialPool, 
		m_meshPool, 
		m_scriptEngine,
		m_physicsWorld, 
		m_modelLoader,
		m_renderer, 
		m_shaderPool,
		m_threadPool
	);
	m_renderer->Initialize(m_graphics, m_timer, m_physicsWorld, m_scene, m_shaderPool, m_materialPool);

	// 15 - GAMEOBJECT POOL
	GameObjectPool::GetInstance().Initialize(
		m_graphics, 
		m_scene, 
		m_renderer, 
		m_meshPool, 
		m_materialPool, 
		m_texturePool,
		m_shaderPool, 
		m_physicsWorld, 
		m_scriptEngine,
		m_threadPool
	);

	// 16 - SCENE	
	m_scene->Initialize();

	// 17 - ENGINE SOCKET
	m_engineSocket = new Socket(this, m_scene, m_renderer, m_input, m_timer, m_modelLoader, m_physicsWorld, m_texturePool, m_graphics);
}

void Engine::Update()
{
	EMIT_SIGNAL(SIGNAL_FRAME_START);

	m_timer->Update();

	//= PHYSICS ==================================
	m_physicsWorld->Step(m_timer->GetDeltaTime());
	//============================================

	//= UPDATE ===================================
	m_input->Update();
	GameObjectPool::GetInstance().Update();
	m_scene->Update();
	//============================================

	//= RENDERING ================================
	m_timer->RenderStart();
	m_renderer->Render();
	m_timer->RenderEnd();
	//============================================

	EMIT_SIGNAL(SIGNAL_FRAME_END);
}

void Engine::Shutdown()
{
	EMIT_SIGNAL(SIGNAL_ENGINE_SHUTDOWN);

	// 17 - ENGINE INTERFACE
	delete m_engineSocket;

	// 16 - SCENE
	delete m_scene;

	// 15 - GAMEOBJECT POOL
	GameObjectPool::GetInstance().Release();

	// 14 - RENDERER
	SafeDelete(m_renderer);

	// 13 - MODEL LOADER
	SafeDelete(m_modelLoader);

	// 12 - MATERIAL POOL
	SafeDelete(m_materialPool);

	// 11 - TEXTURE POOL
	SafeDelete(m_texturePool);

	// 10 - IMAGE LOADER
	ImageImporter::GetInstance().Clear();

	// 9 - MESH POOL
	SafeDelete(m_meshPool);

	// 8 - SHADER POOL
	SafeDelete(m_shaderPool);

	// 7 - SCRIPT ENGINE
	SafeDelete(m_scriptEngine);

	// 6 - PHYSICS ENGINE
	SafeDelete(m_physicsWorld);

	// 5 - INPUT
	SafeDelete(m_input);

	// 4  - GRAPHICS
	SafeDelete(m_graphics);

	// 3 - THREAD POOL
	SafeDelete(m_threadPool);

	// 2 - TIMER
	SafeDelete(m_timer);

	// 1 - LOG
	Log::Release();
}

Socket* Engine::GetSocket()
{
	return m_engineSocket;
}
