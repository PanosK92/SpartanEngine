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
//==================================

using namespace std;

GameObjectPool::GameObjectPool()
{
	m_D3D11Device = nullptr;
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

void GameObjectPool::Initialize(D3D11Device* d3d11Device, Scene* scene, MeshPool* meshPool, MaterialPool* materialPool, TexturePool* texturePool, ShaderPool* shaderPool, PhysicsEngine* physics, ScriptEngine* scriptEngine)
{
	m_D3D11Device = d3d11Device;
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
	vector<GameObject*>::iterator it;
	for (it = m_pool.begin(); it < m_pool.end(); ++it)
	{
		GameObject* gameobject = *it;
		gameobject->Update();
	}
}

void GameObjectPool::Release()
{
	Clear();
}

void GameObjectPool::Clear()
{
	// clear gameobject pool
	vector<GameObject*>::iterator it;
	for (it = m_pool.begin(); it < m_pool.end(); ++it)
	{
		delete *it;
	}
	m_pool.clear();
	m_pool.shrink_to_fit();
}

/*------------------------------------------------------------------------------
									[I/O]
------------------------------------------------------------------------------*/
void GameObjectPool::Save()
{
	// 1st - gameObjectCount
	Serializer::SaveInt(m_pool.size());

	// 2nd - gameObjects
	for (unsigned int i = 0; i < m_pool.size(); i++)
		m_pool[i]->Save();
}

void GameObjectPool::Load()
{
	Clear();

	// 1st - gameObjectCount
	int gameObjectCount = Serializer::LoadInt();

	// create gameObjects
	for (int i = 0; i < gameObjectCount; i++)
		new GameObject(); // no need to add it to the pool manually. The gameobject will call AddGameObject()

	// 2nd - gameObjects
	for (int i = 0; i < gameObjectCount; i++)
		m_pool[i]->Load();

	// resolve hierarchy relationships
	for (int i = 0; i < gameObjectCount; i++)
		m_pool[i]->GetComponent<Transform>()->FindChildren();
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
vector<GameObject*> GameObjectPool::GetAllGameObjects()
{
	return m_pool;
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
			return m_pool[i];
	}

	LOG("Can't return GameObject. No gameObject with name (" + name + ") exists.", Log::Warning);
	return nullptr;
}

GameObject* GameObjectPool::GetGameObjectByIndex(int index)
{
	if (index >= (int)m_pool.size())
	{
		LOG("Can't return GameObject, index out of range.", Log::Warning);
		return nullptr;
	}

	return m_pool[index];
}

GameObject* GameObjectPool::GetGameObjectByID(string ID)
{
	for (unsigned int i = 0; i < m_pool.size(); i++)
	{
		if (m_pool[i]->GetID() == ID)
			return m_pool[i];
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
			gameObjects.push_back(m_pool[i]);
	}

	return gameObjects;
}

GameObject* GameObjectPool::GetRootGameObjectByGameObject(GameObject* gameObject)
{
	GameObject* parent = gameObject;
	if (gameObject->GetTransform()->HasParent())
		parent = gameObject->GetTransform()->GetParent()->g_gameObject;

	if (parent->GetTransform()->HasParent()) // if the parent has a parent, do it again
		GetRootGameObjectByGameObject(parent);

	return parent; // if not, then is is the root gameObject
}

bool GameObjectPool::GameObjectExistsByName(string name)
{
	GameObject* gameObject = nullptr;
	for (unsigned int i = 0; i < m_pool.size(); i++)
	{
		if (m_pool[i]->GetName() == name)
			gameObject = m_pool[i];
	}

	if (gameObject)
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
	vector<GameObject*>::iterator it;
	for (it = m_pool.begin(); it < m_pool.end();)
	{
		GameObject* item = *it;
		if (item->GetID() == gameObject->GetID())
		{
			delete item;
			it = m_pool.erase(it);

			m_scene->MakeDirty();

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
	for (unsigned int i = 0; i < m_pool.size(); i++)
	{
		if (gameObject->GetID() == m_pool[i]->GetID())
			return;
	}

	gameObject->Initialize(m_D3D11Device, m_scene, m_meshPool, m_materialPool, m_texturePool, m_shaderPool, m_physics, m_scriptEngine);
	m_pool.push_back(gameObject);

	m_scene->MakeDirty();
}
