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
#include "Socket.h"
#include "../Pools/GameObjectPool.h"
#include "../Logging/Log.h"
#include "../Graphics/Renderer.h"
#include "../Signals/Signaling.h"
#include "../FileSystem/ModelImporter.h"
#include "../Core/Engine.h"
//==========================================

//= NAMESPACES =====
using namespace std;
//==================

Socket::Socket(Context* context) : Object(context)
{

}

Socket::~Socket()
{
}

void Socket::Initialize()
{
	m_engine = g_context->GetSubsystem<Engine>();
}

//= STATE CONTROL ==============================================================
void Socket::Update()
{
	if (!m_engine)
		return;

	m_engine->Update();
}

void Socket::LightUpdate()
{
	if (!m_engine)
		return;
	m_engine->Update();
}
//=============================================================================


//= IO ========================================================================
void Socket::LoadModel(const string& filePath)
{
	g_context->GetSubsystem<ModelImporter>()->Load(new GameObject(), filePath);
}

void Socket::LoadModelAsync(const string& filePath)
{
	g_context->GetSubsystem<ModelImporter>()->LoadAsync(new GameObject(), filePath);
}

void Socket::SaveSceneToFileAsync(const string& filePath)
{
	return g_context->GetSubsystem<Scene>()->SaveToFileAsync(filePath);
}

void Socket::LoadSceneFromFileAsync(const string& filePath)
{
	return g_context->GetSubsystem<Scene>()->LoadFromFileAsync(filePath);
}

bool Socket::SaveSceneToFile(const string& filePath)
{
	return g_context->GetSubsystem<Scene>()->SaveToFile(filePath);
}

bool Socket::LoadSceneFromFile(const string& filePath)
{
	return g_context->GetSubsystem<Scene>()->LoadFromFile(filePath);
}
//==============================================================================

//= GRAPHICS ===================================================================
void Socket::SetViewport(int width, int height) const
{
	g_context->GetSubsystem<Renderer>()->SetResolution(width, height);
}
//==============================================================================

//= MISC =======================================================================
void Socket::SetPhysicsDebugDraw(bool enable)
{
	//m_renderer->SetPhysicsDebugDraw(enable);
}

PhysicsDebugDraw* Socket::GetPhysicsDebugDraw()
{
	return g_context->GetSubsystem<PhysicsWorld>()->GetPhysicsDebugDraw();
}

void Socket::ClearScene()
{
	g_context->GetSubsystem<Scene>()->Clear();
}

ImageImporter* Socket::GetImageLoader()
{
	return &ImageImporter::GetInstance();
}

void Socket::SetLogger(weak_ptr<ILogger> logger)
{
	Log::SetLogger(logger);
}
//==============================================================================

//= GAMEOBJECTS ================================================================
vector<GameObject*> Socket::GetAllGameObjects()
{
	return GameObjectPool::GetInstance().GetAllGameObjects();
}

vector<GameObject*> Socket::GetRootGameObjects()
{
	return GameObjectPool::GetInstance().GetRootGameObjects();
}

GameObject* Socket::GetGameObjectByID(string gameObjectID)
{
	return GameObjectPool::GetInstance().GetGameObjectByID(gameObjectID);
}

int Socket::GetGameObjectCount()
{
	return GameObjectPool::GetInstance().GetGameObjectCount();
}

void Socket::DestroyGameObject(GameObject* gameObject)
{
	if (!gameObject)
		return;

	GameObjectPool::GetInstance().RemoveGameObject(gameObject);
}

bool Socket::GameObjectExists(GameObject* gameObject)
{
	if (!gameObject)
		return false;

	bool exists = GameObjectPool::GetInstance().GameObjectExists(gameObject);

	return exists;
}
//==============================================================================

//= STATS ======================================================================
float Socket::GetFPS() const
{
	return g_context->GetSubsystem<Timer>()->GetFPS();
}

int Socket::GetRenderedMeshesCount() const
{
	return g_context->GetSubsystem<Renderer>()->GetRenderedMeshesCount();
}

float Socket::GetDeltaTime() const
{
	return g_context->GetSubsystem<Timer>()->GetDeltaTimeMs();
}

float Socket::GetRenderTime() const
{
	return g_context->GetSubsystem<Timer>()->GetRenderTimeMs();
}
//==============================================================================