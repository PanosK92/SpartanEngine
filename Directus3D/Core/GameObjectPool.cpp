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
#include "../Events/EventHandler.h"
#include "../Core/Context.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

GameObjectPool::GameObjectPool()
{
	m_context = nullptr;

	// Subcribe to render events
	SUBSCRIBE_TO_EVENT(EVENT_ENGINE_START, std::bind(&GameObjectPool::Start, this));	
	SUBSCRIBE_TO_EVENT(RENDER_UPDATE, std::bind(&GameObjectPool::Update, this));
}

GameObjectPool::~GameObjectPool()
{
	Clear();
}

void GameObjectPool::Initialize(Context* context)
{
	m_context = context;
}

void GameObjectPool::Start()
{
	for (const auto& gameObject : m_gameObjects)
		gameObject->Start();
}

void GameObjectPool::Update()
{
	for (const auto& gameObject : m_gameObjects)
		gameObject->Update();
}

void GameObjectPool::Release()
{
	Clear();
}

void GameObjectPool::Clear()
{
	for (auto& gameObject : m_gameObjects)
		delete gameObject;

	m_gameObjects.clear();
	m_gameObjects.shrink_to_fit();
}

/*------------------------------------------------------------------------------
									[I/O]
------------------------------------------------------------------------------*/
void GameObjectPool::Serialize()
{
	auto gameObjects = GetAllGameObjects();

	// 1st - GameObject count
	Serializer::WriteInt((int)gameObjects.size());

	// 2nd - GameObject IDs
	for (const auto& gameObject : gameObjects)
		Serializer::WriteSTR(gameObject->GetID());

	// 3rd - GameObjects
	for (const auto& gameObject : gameObjects)
		gameObject->Serialize();
}

void GameObjectPool::Deserialize()
{
	Clear();

	// 1st - GameObject count
	int gameObjectCount = Serializer::ReadInt();

	// 2nd - GameObject IDs
	for (int i = 0; i < gameObjectCount; i++)
	{
		GameObject* gameObject = new GameObject();
		gameObject->SetID(Serializer::ReadSTR());
	}

	// 3rd - GameObjects
	for (const auto& gameObject : m_gameObjects)
		gameObject->Deserialize();
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
vector<GameObject*> GameObjectPool::GetAllGameObjects()
{
	vector<GameObject*> gameObjects;
	for (auto gameObject : m_gameObjects)
		gameObjects.push_back(gameObject);

	return gameObjects;
}

vector<GameObject*> GameObjectPool::GetRootGameObjects()
{
	vector<GameObject*> rootGameObjects;
	for (const auto& gameObject : m_gameObjects)
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
	return m_gameObjects.size();
}

int GameObjectPool::GetGameObjectIndex(GameObject* gameObject)
{
	if (!gameObject)
	{
		LOG_WARNING("Can't return GameObject index, the gameObject is null.");
		return -1;
	}

	for (auto i = 0; i < m_gameObjects.size(); i++)
		if (gameObject->GetID() == m_gameObjects[i]->GetID())
			return i;

	LOG_WARNING("Can't return GameObject index, the gameObject is not contained in the pool.");
	return -1;
}

GameObject* GameObjectPool::GetGameObjectByName(const string& name)
{
	for (auto i = 0; i < m_gameObjects.size(); i++)
	{
		if (m_gameObjects[i]->GetName() == name)
			return m_gameObjects[i];
	}

	LOG_WARNING("Can't return GameObject. No gameObject with name (" + name + ") exists.");
	return nullptr;
}

GameObject* GameObjectPool::GetGameObjectByIndex(int index)
{
	if (index >= int(m_gameObjects.size()))
	{
		LOG_WARNING("Can't return GameObject, index out of range.");
		return nullptr;
	}

	return m_gameObjects[index];
}

GameObject* GameObjectPool::GetGameObjectByID(const string& ID)
{
	for (auto i = 0; i < m_gameObjects.size(); i++)
	{
		if (m_gameObjects[i]->GetID() == ID)
			return m_gameObjects[i];
	}

	return nullptr;
}

const vector<GameObject*>& GameObjectPool::GetGameObjectsByParentID(const string& ID)
{
	vector<GameObject*> gameObjects;
	for (auto gameObject : m_gameObjects)
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

	for (auto gameObject : m_gameObjects)
		if (gameObject->GetID() == gameObjectIn->GetID())
			return true;

	return false;
}

bool GameObjectPool::GameObjectExistsByName(const string& name)
{
	for (auto gameObject : m_gameObjects)
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
	for (auto it = m_gameObjects.begin(); it < m_gameObjects.end();)
	{
		GameObject* temp = *it;
		if (temp->GetID() == gameObject->GetID())
		{
			delete temp;
			it = m_gameObjects.erase(it);
			FIRE_EVENT(RESOLVE_HIERARCHY);
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
	for (auto gameObject : m_gameObjects)
		if (gameObjectIn->GetID() == gameObject->GetID())
			return;

	gameObjectIn->Initialize(m_context);
	m_gameObjects.push_back(gameObjectIn);

	FIRE_EVENT(RESOLVE_HIERARCHY);
}
