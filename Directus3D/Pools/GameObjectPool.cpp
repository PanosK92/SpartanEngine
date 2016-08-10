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

//= INCLUDES =======================
#include "GameObjectPool.h"
#include "../Core/GameObject.h"
#include "../IO/Serializer.h"
#include "../IO/Log.h"
#include "../Components/Transform.h"
#include "../Core/Scene.h"
#include "../Signals/Signaling.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

GameObjectPool::GameObjectPool()
{
	m_graphicsDevice = nullptr;
	m_scene = nullptr;
	m_meshPool = nullptr;
	m_materialPool = nullptr;
	m_texturePool = nullptr;
	m_shaderPool = nullptr;
	m_physics = nullptr;
	m_scriptEngine = nullptr;
}

GameObjectPool::~GameObjectPool()
{
	DeleteAll();
}

void GameObjectPool::Initialize(GraphicsDevice* d3d11Device, Scene* scene, Renderer* renderer, MeshPool* meshPool, MaterialPool* materialPool, TexturePool* texturePool, ShaderPool* shaderPool, PhysicsWorld* physics, ScriptEngine* scriptEngine)
{
	m_graphicsDevice = d3d11Device;
	m_scene = scene;
	m_renderer = renderer;
	m_meshPool = meshPool;
	m_materialPool = materialPool;
	m_texturePool = texturePool;
	m_shaderPool = shaderPool;
	m_physics = physics;
	m_scriptEngine = scriptEngine;

	CONNECT_TO_SIGNAL(SIGNAL_ENGINE_START, std::bind(&GameObjectPool::Start, this));
}

void GameObjectPool::Start()
{
	// call gameobject Start()
	for (auto it = m_gameObjectPool.begin(); it < m_gameObjectPool.end(); ++it)
		(*it)->Start();
}

void GameObjectPool::Update()
{
	// call gameobject Update()
	for (auto it = m_gameObjectPool.begin(); it < m_gameObjectPool.end(); ++it)
		(*it)->Update();
}

void GameObjectPool::Release()
{
	DeleteAll();
}

void GameObjectPool::DeleteAll()
{
	for (int i = 0; i < m_gameObjectPool.size(); i++)
		delete m_gameObjectPool[i];

	m_gameObjectPool.clear();
	m_gameObjectPool.shrink_to_fit();
}

/*------------------------------------------------------------------------------
									[I/O]
------------------------------------------------------------------------------*/
void GameObjectPool::Serialize()
{
	// 1st - GameObject count
	Serializer::SaveInt(m_gameObjectPool.size());

	// 2nd - GameObject IDs
	for (auto i = 0; i < m_gameObjectPool.size(); i++)
		Serializer::SaveSTR(m_gameObjectPool[i]->GetID());

	// 3rd - GameObjects
	for (auto i = 0; i < m_gameObjectPool.size(); i++)
		m_gameObjectPool[i]->Serialize();
}

void GameObjectPool::Deserialize()
{
	DeleteAll();

	// 1st - GameObject count
	int gameObjectCount = Serializer::LoadInt();

	// 2nd - GameObject IDs
	for (auto i = 0; i < gameObjectCount; i++)
	{
		GameObject* gameObject = new GameObject();
		gameObject->SetID(Serializer::LoadSTR());
	}

	// 3rd - GameObjects
	for (auto i = 0; i < gameObjectCount; i++)
		m_gameObjectPool[i]->Deserialize();
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
vector<GameObject*> GameObjectPool::GetAllGameObjects()
{
	vector<GameObject*> gameObjects;
	for (auto i = 0; i < m_gameObjectPool.size(); i++)
		gameObjects.push_back(m_gameObjectPool[i]);

	return gameObjects;
}

vector<GameObject*> GameObjectPool::GetRootGameObjects()
{
	vector<GameObject*> rootGameObjects;
	for (auto i = 0; i < m_gameObjectPool.size(); i++)
	{
		if (m_gameObjectPool[i]->GetTransform()->IsRoot())
			rootGameObjects.push_back(m_gameObjectPool[i]);
	}

	return rootGameObjects;
}

GameObject* GameObjectPool::GetGameObjectRoot(GameObject* gameObject)
{
	if (!gameObject)
		return nullptr;

	return gameObject->GetTransform()->GetRoot()->GetGameObject();
}

int GameObjectPool::GetGameObjectCount()
{
	return m_gameObjectPool.size();
}

int GameObjectPool::GetGameObjectIndex(GameObject* gameObject)
{
	if (gameObject == nullptr)
	{
		LOG("Can't return GameObject index, the gameObject is null.", Log::Warning);
		return -1;
	}

	for (unsigned int i = 0; i < m_gameObjectPool.size(); i++)
		if (gameObject->GetID() == m_gameObjectPool[i]->GetID())
			return i;

	LOG_WARNING("Can't return GameObject index, the gameObject is not contained in the pool.");
	return -1;
}

GameObject* GameObjectPool::GetGameObjectByName(string name)
{
	for (unsigned int i = 0; i < m_gameObjectPool.size(); i++)
	{
		if (m_gameObjectPool[i]->GetName() == name)
			return m_gameObjectPool[i];
	}

	LOG_WARNING("Can't return GameObject. No gameObject with name (" + name + ") exists.");
	return nullptr;
}

GameObject* GameObjectPool::GetGameObjectByIndex(int index)
{
	if (index >= int(m_gameObjectPool.size()))
	{
		LOG_WARNING("Can't return GameObject, index out of range.");
		return nullptr;
	}

	return m_gameObjectPool[index];
}

GameObject* GameObjectPool::GetGameObjectByID(string ID)
{
	for (unsigned int i = 0; i < m_gameObjectPool.size(); i++)
	{
		if (m_gameObjectPool[i]->GetID() == ID)
			return m_gameObjectPool[i];
	}

	return nullptr;
}

const vector<GameObject*>& GameObjectPool::GetGameObjectsByParentID(string ID)
{
	vector<GameObject*> gameObjects;
	for (unsigned int i = 0; i < m_gameObjectPool.size(); i++)
	{
		// check if the gameobject has a parent
		if (!m_gameObjectPool[i]->GetTransform()->HasParent())
			continue;

		// if it does, check it's ID
		if (m_gameObjectPool[i]->GetTransform()->GetParent()->g_gameObject->GetID() == ID)
			gameObjects.push_back(m_gameObjectPool[i]);
	}

	return gameObjects;
}

bool GameObjectPool::GameObjectExists(GameObject* gameObject)
{
	if (!gameObject)
		return false;

	for (unsigned int i = 0; i < m_gameObjectPool.size(); i++)
		if (m_gameObjectPool[i]->GetID() == gameObject->GetID())
			return true;

	return false;
}

bool GameObjectPool::GameObjectExistsByName(string name)
{
	for (unsigned int i = 0; i < m_gameObjectPool.size(); i++)
		if (m_gameObjectPool[i]->GetName() == name)
			return true;

	return false;
}

// Removes a gameobject and all of it's children
void GameObjectPool::RemoveGameObject(GameObject* gameObject)
{
	if (!gameObject) return; // make sure it's not null

	// remove any descendants
	vector<Transform*> descendants = gameObject->GetTransform()->GetDescendants();
	for (unsigned int i = 0; i < descendants.size(); i++)
	{
		GameObject* descendant = descendants[i]->GetGameObject();
		RemoveSingleGameObject(descendant);
	}

	// remove this gameobject but keep it's parent
	Transform* parent = gameObject->GetTransform()->GetParent();
	RemoveSingleGameObject(gameObject);

	// if there is a parent, update it's children pool
	if (parent)
		parent->FindChildren();
}

// Removes a gameobject but leaves the parent and the children as is
void GameObjectPool::RemoveSingleGameObject(GameObject* gameObject)
{
	for (auto it = m_gameObjectPool.begin(); it < m_gameObjectPool.end();)
	{
		GameObject* temp = *it;
		if (temp->GetID() == gameObject->GetID())
		{
			delete temp;
			it = m_gameObjectPool.erase(it);
			m_scene->AnalyzeGameObjects();
			return;
		}
		++it;
	}
}

/*------------------------------------------------------------------------------
							[CALLED BY GAMEOBJECTS]
------------------------------------------------------------------------------*/
void GameObjectPool::AddGameObjectToPool(GameObject* gameObject)
{
	// check if it already exists.
	for (unsigned int i = 0; i < m_gameObjectPool.size(); i++)
	{
		if (gameObject->GetID() == m_gameObjectPool[i]->GetID())
			return;
	}

	gameObject->Initialize(m_graphicsDevice, m_scene, m_renderer, m_meshPool, m_materialPool, m_texturePool, m_shaderPool, m_physics, m_scriptEngine);
	m_gameObjectPool.push_back(gameObject);

	m_scene->AnalyzeGameObjects();
}
