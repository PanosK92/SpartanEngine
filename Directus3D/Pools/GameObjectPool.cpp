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
//==================================

using namespace std;

GameObjectPool::GameObjectPool()
{
	m_graphicsDevice = nullptr;
	m_scene = nullptr;
	m_meshPool = nullptr;
	m_materialPool = nullptr;
	m_physics = nullptr;
	m_scriptEngine = nullptr;
}

GameObjectPool::~GameObjectPool()
{
	Clear();
}

void GameObjectPool::Initialize(GraphicsDevice* d3d11Device, Scene* scene, MeshPool* meshPool, MaterialPool* materialPool, TexturePool* texturePool, ShaderPool* shaderPool, PhysicsEngine* physics, ScriptEngine* scriptEngine)
{
	m_graphicsDevice = d3d11Device;
	m_scene = scene;
	m_meshPool = meshPool;
	m_materialPool = materialPool;
	m_texturePool = texturePool;
	m_shaderPool = shaderPool;
	m_physics = physics;
	m_scriptEngine = scriptEngine;
}

void GameObjectPool::Update()
{
	// update gameobjects
	vector<unique_ptr<GameObject>>::iterator it;
	for (it = m_pool.begin(); it < m_pool.end(); ++it)
		(*it)->Update();
}

void GameObjectPool::Release()
{
	Clear();
}

void GameObjectPool::Clear()
{
	m_pool.clear();
	m_pool.shrink_to_fit();
}

/*------------------------------------------------------------------------------
									[I/O]
------------------------------------------------------------------------------*/
void GameObjectPool::Serialize()
{
	// 1st - gameObjectCount
	Serializer::SaveInt(m_pool.size());

	// 2nd - gameObjects
	for (unsigned int i = 0; i < m_pool.size(); i++)
		m_pool[i]->Serialize();
}

void GameObjectPool::Deserialize()
{
	Clear();

	// 1st - gameObjectCount
	int gameObjectCount = Serializer::LoadInt();

	// 2nd - gameObjects
	for (int i = 0; i < gameObjectCount; i++)
	{
		// The gameobject will add itself in the pool, 
		// no need to do it here or call delete on it.
		GameObject* gameObject = new GameObject();
		gameObject->Deserialize();
	}

	// resolve hierarchy relationships
	for (int i = 0; i < gameObjectCount; i++)
		m_pool[i]->GetComponent<Transform>()->FindChildren();
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
vector<GameObject*> GameObjectPool::GetAllGameObjects()
{
	vector<GameObject*> gameObjects;
	for (auto i = 0; i < m_pool.size(); i++)
		gameObjects.push_back(m_pool[i].get());

	return gameObjects;
}

vector<GameObject*> GameObjectPool::GetRootGameObjects()
{
	vector<GameObject*> rootGameObjects;
	for (auto i = 0; i < m_pool.size(); i++)
	{
		if (m_pool[i]->GetTransform()->IsRoot())
			rootGameObjects.push_back(m_pool[i].get());
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
	return m_pool.size();
}

int GameObjectPool::GetGameObjectIndex(GameObject* gameObject)
{
	if (gameObject == nullptr)
	{
		LOG("Can't return GameObject index, the gameObject is null.", Log::Warning);
		return -1;
	}

	for (unsigned int i = 0; i < m_pool.size(); i++)
		if (gameObject->GetID() == m_pool[i]->GetID())
			return i;

	LOG("Can't return GameObject index, the gameObject is not contained in the pool.", Log::Warning);
	return -1;
}

GameObject* GameObjectPool::GetGameObjectByName(string name)
{
	for (unsigned int i = 0; i < m_pool.size(); i++)
	{
		if (m_pool[i]->GetName() == name)
			return m_pool[i].get();
	}

	LOG("Can't return GameObject. No gameObject with name (" + name + ") exists.", Log::Warning);
	return nullptr;
}

GameObject* GameObjectPool::GetGameObjectByIndex(int index)
{
	if (index >= int(m_pool.size()))
	{
		LOG("Can't return GameObject, index out of range.", Log::Warning);
		return nullptr;
	}

	return m_pool[index].get();
}

GameObject* GameObjectPool::GetGameObjectByID(string ID)
{
	for (unsigned int i = 0; i < m_pool.size(); i++)
	{
		if (m_pool[i]->GetID() == ID)
			return m_pool[i].get();
	}

	return nullptr;
}

vector<GameObject*> GameObjectPool::GetGameObjectsByParentID(string ID)
{
	vector<GameObject*> gameObjects;
	for (unsigned int i = 0; i < m_pool.size(); i++)
	{
		// check if the gameobject has a parent
		if (!m_pool[i]->GetTransform()->HasParent())
			continue;

		// if it does, check it's ID
		if (m_pool[i]->GetTransform()->GetParent()->g_gameObject->GetID() == ID)
			gameObjects.push_back(m_pool[i].get());
	}

	return gameObjects;
}

bool GameObjectPool::GameObjectExists(GameObject* gameObject)
{
	if (!gameObject)
		return false;

	for (unsigned int i = 0; i < m_pool.size(); i++)
		if (m_pool[i]->GetID() == gameObject->GetID())
			return true;

	return false;
}

bool GameObjectPool::GameObjectExistsByName(string name)
{
	for (unsigned int i = 0; i < m_pool.size(); i++)
		if (m_pool[i]->GetName() == name)
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
	vector<unique_ptr<GameObject>>::iterator it;
	for (it = m_pool.begin(); it < m_pool.end();)
	{
		if ((*it)->GetID() == gameObject->GetID())
		{
			it = m_pool.erase(it);
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
	unique_ptr<GameObject> smartPtrGameObject(gameObject);

	// check if it already exists.
	for (unsigned int i = 0; i < m_pool.size(); i++)
	{
		if (gameObject->GetID() == m_pool[i]->GetID())
			return;
	}

	gameObject->Initialize(m_graphicsDevice, m_scene, m_meshPool, m_materialPool, m_texturePool, m_shaderPool, m_physics, m_scriptEngine);
	m_pool.push_back(move(smartPtrGameObject));

	m_scene->AnalyzeGameObjects();
}
