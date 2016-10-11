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
#include "Socket.h"
#include "Scene.h"
#include "Timer.h"
#include "../Pools/GameObjectPool.h"
#include "../IO/Log.h"
#include "../Components/MeshRenderer.h"
#include "../Graphics/Renderer.h"
#include "../AssetImporting/ModelImporter.h"
#include "../IO/FileSystem.h"
#include "Settings.h"
#include "../Signals/Signaling.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

Socket::Socket(Engine* engine, Scene* scene, Renderer* renderer, Input* input, Timer* timer, ModelImporter* modelLoader, PhysicsWorld* physics, TexturePool* texturePool, Graphics* graphicsDevice)
{
	m_engine = engine;
	m_scene = scene;
	m_renderer = renderer;
	m_timer = timer;
	m_modelLoader = modelLoader;
	m_physics = physics;
	m_texturePool = texturePool;
	m_graphics = graphicsDevice;
	m_input = input;
}

Socket::~Socket()
{
}

//= STATE CONTROL ==============================================================
void Socket::StartEngine() const
{
	SET_ENGINE_MODE(Editor_Playing);
	EMIT_SIGNAL(SIGNAL_ENGINE_START);
}

void Socket::StopEngine()
{
	SET_ENGINE_MODE(Editor_Idle);
	EMIT_SIGNAL(SIGNAL_ENGINE_STOP);
}

void Socket::Update() const
{
	m_engine->Update();
}

//=============================================================================

//= IO ========================================================================
void Socket::SetLogger(ILogger* logger)
{
	Log::SetLogger(logger);
}

void Socket::LoadModel(string path)
{
	m_modelLoader->Load(path, new GameObject());
}

ImageImporter* Socket::GetImageLoader()
{
	return &ImageImporter::GetInstance();
}
//==============================================================================

//= GRAPHICS ===================================================================
void Socket::SetViewport(int width, int height) const
{
	m_renderer->SetResolution(width, height);
}
//==============================================================================

//= MISC =======================================================================
void Socket::SetPhysicsDebugDraw(bool enable)
{
	//m_renderer->SetPhysicsDebugDraw(enable);
}

PhysicsDebugDraw* Socket::GetPhysicsDebugDraw()
{
	return m_physics->GetPhysicsDebugDraw();
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

//= SCENE ======================================================================
bool Socket::SaveSceneToFile(string path)
{
	return m_scene->SaveToFile(path);
}

bool Socket::LoadSceneFromFile(string path)
{
	m_timer->Reset();
	return m_scene->LoadFromFile(path);
}

void Socket::ClearScene()
{
	m_scene->Clear();
}
//==============================================================================

//= STATS ======================================================================
float Socket::GetFPS() const
{
	return m_timer->GetFPS();
}

int Socket::GetRenderedMeshesCount() const
{
	return m_renderer->GetRenderedMeshesCount();
}

float Socket::GetDeltaTime() const
{
	return m_timer->GetDeltaTime();
}

float Socket::GetRenderTime() const
{
	return m_timer->GetRenderTime();
}
//==============================================================================

void Socket::SetMaterialTexture(GameObject* gameObject, TextureType type, string texturePath)
{
	if (!gameObject)
		return;

	MeshRenderer* meshRenderer = gameObject->GetComponent<MeshRenderer>();
	if (!meshRenderer)
		return;

	shared_ptr<Material> material = meshRenderer->GetMaterial();
	if (material)
	{
		// Get the texture from the texture pool
		shared_ptr<Texture> texture = m_texturePool->GetTextureByPath(texturePath);

		// If it's not loaded yet, load it
		if (!texture)
		{
			texture = m_texturePool->Add(texturePath);
			texture->SetType(type);
		}

		// Set it to the material
		material->SetTextureByID(texture->GetID());

		return;
	}

	LOG_WARNING("Unable to set texture: \"" + texturePath +"\" to material");
}
