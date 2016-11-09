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

//= INCLUDES ===========================
#include "GameObject.h"
#include "Scene.h"
#include "../Core/GUIDGenerator.h"
#include "../Pools/GameObjectPool.h"
#include "../IO/Serializer.h"
#include "../Components/Transform.h"
#include "../Components/MeshFilter.h"
#include "../Components/MeshRenderer.h"
#include "../Components/Camera.h"
#include "../Components/Skybox.h"
#include "../Components/RigidBody.h"
#include "../Components/Collider.h"
#include "../Components/MeshCollider.h"
#include "../Components/Hinge.h"
#include "../Components/Script.h"
#include "../Components/LineRenderer.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

GameObject::GameObject()
{
	m_ID = GENERATE_GUID;
	m_name = "GameObject";
	m_isActive = true;
	m_hierarchyVisibility = true;

	GameObjectPool::GetInstance().AddGameObjectToPool(this);

	// add transform component
	m_transform = AddComponent<Transform>();
}

GameObject::~GameObject()
{
	// delete components
	for (auto it = m_components.begin(); it != m_components.end(); ++it)
	{
		IComponent* component = it->second;
		component->Remove();
		delete component;
		it = m_components.erase(it);
	}
	m_components.clear();

	m_ID.clear();
	m_name.clear();
	m_isActive = true;
	m_hierarchyVisibility = true;
}

void GameObject::Initialize(Context* context)
{
	m_context = context;
}

void GameObject::Start()
{
	// call component Start()
	for (auto const it : m_components)
		it.second->Start();
}

void GameObject::Update()
{
	if (!m_isActive)
		return;

	// call component Update()
	for (auto const& it : m_components)
		it.second->Update();
}

bool GameObject::Save(const string& filePath)
{
	if (!Serializer::StartWriting(filePath))
		return false;

	Serialize();
	Serializer::StopReading();

	return true;
}

void GameObject::Serialize()
{
	Serializer::WriteSTR(m_ID);
	Serializer::WriteSTR(m_name);
	Serializer::WriteBool(m_isActive);
	Serializer::WriteBool(m_hierarchyVisibility);

	Serializer::WriteInt(m_components.size());
	for (auto const& it : m_components)
	{
		Serializer::WriteSTR(it.first); // save component's type
		Serializer::WriteSTR(it.second->g_ID); // save component's id
	}

	for (auto const& it : m_components)
		it.second->Serialize();
}

void GameObject::Deserialize()
{
	m_ID = Serializer::ReadSTR();
	m_name = Serializer::ReadSTR();
	m_isActive = Serializer::ReadBool();
	m_hierarchyVisibility = Serializer::ReadBool();

	int componentCount = Serializer::ReadInt();
	for (int i = 0; i < componentCount; i++)
	{
		string type = Serializer::ReadSTR(); // load component's type
		string id = Serializer::ReadSTR(); // laad component's id

		IComponent* component = AddComponentBasedOnType(type);
		component->g_ID = id;
	}

	// Sometimes there are component dependencies, e.g. a collider that needs
	// to set it's shape to a rigibody. So, it's important to first create all 
	// the components (like above) and then deserialize them (like here).
	for (auto it = m_components.begin(); it != m_components.end(); ++it)
		it->second->Deserialize();
}

void GameObject::RemoveComponentByID(const string& id)
{
	for (auto it = m_components.begin(); it != m_components.end();)
	{
		IComponent* component = it->second;
		if (component->g_ID == id)
		{
			component->Remove();
			delete component;
			it = m_components.erase(it);
			return;
		}
		++it;
	}

	m_context->GetSubsystem<Scene>()->Resolve();
}

Transform* GameObject::GetTransform()
{
	return m_transform;
}

//= HELPER FUNCTIONS ===========================================
IComponent* GameObject::AddComponentBasedOnType(const string& typeStr)
{
	IComponent* component = nullptr;

	if (typeStr == "Transform")
		component = AddComponent<Transform>();

	if (typeStr == "MeshFilter")
		component = AddComponent<MeshFilter>();

	if (typeStr == "MeshRenderer")
		component = AddComponent<MeshRenderer>();

	if (typeStr == "Light")
		component = AddComponent<Light>();

	if (typeStr == "Camera")
		component = AddComponent<Camera>();

	if (typeStr == "Skybox")
		component = AddComponent<Skybox>();

	if (typeStr == "RigidBody")
		component = AddComponent<RigidBody>();

	if (typeStr == "Collider")
		component = AddComponent<Collider>();

	if (typeStr == "MeshCollider")
		component = AddComponent<MeshCollider>();

	if (typeStr == "Hinge")
		component = AddComponent<Hinge>();

	if (typeStr == "Script")
		component = AddComponent<Script>();

	if (typeStr == "LineRenderer")
		component = AddComponent<LineRenderer>();

	return component;
}