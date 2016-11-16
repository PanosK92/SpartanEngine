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
#include "../Logging/Log.h"
#include "../Components/Transform.h"
#include "../Core/Scene.h"
#include "../Events/EventHandler.h"
#include "../Core/Context.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

GameObjectPool::GameObjectPool()
{
	m_context = nullptr;
}

GameObjectPool::~GameObjectPool()
{
	Clear();
}

void GameObjectPool::Initialize(Context* context)
{
	m_context = context;
	SUBSCRIBE_TO_EVENT(EVENT_ENGINE_START, std::bind(&GameObjectPool::Start, this));
}

void GameObjectPool::Start()
{
	for (const auto& gameObject : m_gameObjectPool)
		gameObject->Start();
}

void GameObjectPool::Update()
{
	for (const auto& gameObject : m_gameObjectPool)
		gameObject->Update();
}

void GameObjectPool::Release()
{
	Clear();
}

void GameObjectPool::Clear()
{
	for (auto& gameObject : m_gameObjectPool)
		delete gameObject;

	m_gameObjectPool.clear();
	m_gameObjectPool.shrink_to_fit();
}

/*------------------------------------------------------------------------------
									[I/O]
------------------------------------------------------------------------------*/
void GameObjectPool::Serialize()
{
	auto rootGameObjects = GetRootGameObjects();
	Serializer::WriteVectorGameObject(rootGameObjects);
}

void GameObjectPool::Deserialize()
{
	Clear();
	m_gameObjectPool = Serializer::ReadVectorGameObject();
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
vector<GameObject*> GameObjectPool::GetAllGameObjects()
{
	vector<GameObject*> gameObjects;
	for (auto gameObject : m_gameObjectPool)
		gameObjects.push_back(gameObject);

	return gameObjects;
}

vector<GameObject*> GameObjectPool::GetRootGameObjects()
{
	vector<GameObject*> rootGameObjects;
	for (const auto& gameObject : m_gameObjectPool)
	{
		if (gameObject->GetTransform()->IsRoot())
			rootGameObjects.push_back(gameObject);
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
	if (!gameObject)
	{
		LOG_WARNING("Can't return GameObject index, the gameObject is null.");
		return -1;
	}

	for (auto i = 0; i < m_gameObjectPool.size(); i++)
		if (gameObject->GetID() == m_gameObjectPool[i]->GetID())
			return i;

	LOG_WARNING("Can't return GameObject index, the gameObject is not contained in the pool.");
	return -1;
}

GameObject* GameObjectPool::GetGameObjectByName(const string& name)
{
	for (auto i = 0; i < m_gameObjectPool.size(); i++)
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

GameObject* GameObjectPool::GetGameObjectByID(const string& ID)
{
	for (auto i = 0; i < m_gameObjectPool.size(); i++)
	{
		if (m_gameObjectPool[i]->GetID() == ID)
			return m_gameObjectPool[i];
	}

	return nullptr;
}

const vector<GameObject*>& GameObjectPool::GetGameObjectsByParentID(const string& ID)
{
	vector<GameObject*> gameObjects;
	for (auto gameObject : m_gameObjectPool)
	{
		// check if the gameobject has a parent
		if (!gameObject->GetTransform()->HasParent())
			continue;

		// if it does, check it's ID
		if (gameObject->GetTransform()->GetParent()->g_gameObject->GetID() == ID)
			gameObjects.push_back(gameObject);
	}

	return gameObjects;
}

bool GameObjectPool::GameObjectExists(GameObject* gameObjectIn)
{
	if (!gameObjectIn)
		return false;

	for (auto gameObject : m_gameObjectPool)
		if (gameObject->GetID() == gameObjectIn->GetID())
			return true;

	return false;
}

bool GameObjectPool::GameObjectExistsByName(const string& name)
{
	for (auto gameObject : m_gameObjectPool)
		if (gameObject->GetName() == name)
			return true;

	return false;
}

// Removes a gameobject and all of it's children
void GameObjectPool::RemoveGameObject(GameObject* gameObject)
{
	if (!gameObject)
		return;

	// remove any descendants
	for (auto transform : gameObject->GetTransform()->GetDescendants())
	{
		GameObject* descendant = transform->GetGameObject();
		RemoveSingleGameObject(descendant);
	}

	// remove this gameobject but keep it's parent
	Transform* parent = gameObject->GetTransform()->GetParent();
	RemoveSingleGameObject(gameObject);

	// if there is a parent, update it's children pool
	if (parent)
		parent->ResolveChildrenRecursively();
}

// Removes a gameobject but leaves the parent and the children as is
void GameObjectPool::RemoveSingleGameObject(GameObject* gameObject)
{
	Scene* scene = m_context->GetSubsystem<Scene>();
	for (auto it = m_gameObjectPool.begin(); it < m_gameObjectPool.end();)
	{
		GameObject* temp = *it;
		if (temp->GetID() == gameObject->GetID())
		{
			delete temp;
			it = m_gameObjectPool.erase(it);
			scene->Resolve();
			return;
		}
		++it;
	}
}

/*------------------------------------------------------------------------------
							[CALLED BY GAMEOBJECTS]
------------------------------------------------------------------------------*/
void GameObjectPool::AddGameObjectToPool(GameObject* gameObjectIn)
{
	// check if it already exists.
	for (auto gameObject : m_gameObjectPool)
		if (gameObjectIn->GetID() == gameObject->GetID())
			return;

	gameObjectIn->Initialize(m_context);
	m_gameObjectPool.push_back(gameObjectIn);

	m_context->GetSubsystem<Scene>()->Resolve();
}
