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
#include "../IO/Log.h"
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
	for (auto it = m_components.begin(); it != m_components.end(); ++it)
		it->second->Start();
}

void GameObject::Update()
{
	if (!m_isActive)
		return;

	// call component Update()
	for (auto it = m_components.begin(); it != m_components.end(); ++it)
		it->second->Update();
}

string GameObject::GetName()
{
	return m_name;
}

void GameObject::SetName(const string& name)
{
	m_name = name;
}

string GameObject::GetID()
{
	return m_ID;
}

void GameObject::SetID(const string& ID)
{
	m_ID = ID;
}

void GameObject::SetActive(bool active)
{
	m_isActive = active;
}

bool GameObject::IsActive()
{
	return m_isActive;
}

void GameObject::SetHierarchyVisibility(bool value)
{
	m_hierarchyVisibility = value;
}

bool GameObject::IsVisibleInHierarchy()
{
	return m_hierarchyVisibility;
}

void GameObject::Serialize()
{
	Serializer::WriteSTR(m_ID);
	Serializer::WriteSTR(m_name);
	Serializer::WriteBool(m_isActive);
	Serializer::WriteBool(m_hierarchyVisibility);

	Serializer::WriteInt(m_components.size());
	for (auto it = m_components.begin(); it != m_components.end(); ++it)
	{
		Serializer::WriteSTR(it->first); // save component's type
		Serializer::WriteSTR(it->second->g_ID); // save component's id
	}

	for (auto it = m_components.begin(); it != m_components.end(); ++it)
		it->second->Serialize();
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

template <class Type>
Type* GameObject::AddComponent()
{
	// Convert class Type to a string.
	string typeStr(typeid(Type).name());
	typeStr = typeStr.substr(typeStr.find_first_of(" \t") + 1); // remove word "class".

	// Check if a component of that type already exists
	IComponent* existingComp = GetComponent<Type>();
	if (existingComp)
	{
		// If it's anything but a script, it can't have multiple
		// instances return the existing component.
		if (typeStr != "Script")
			return dynamic_cast<Type*>(existingComp);
	}

	// Get the created component.
	IComponent* component = new Type;

	// Add the component.
	m_components.insert(pair<string, IComponent*>(typeStr, component));

	// Set default properties.
	component->g_ID = GENERATE_GUID;
	component->g_enabled = true;

	// Set some useful pointers.
	component->g_gameObject = this;
	component->g_transform = GetTransform();
	component->g_context = m_context;

	// Run Initialize().
	component->Initialize();

	// Do a scene analysis
	m_context->GetSubsystem<Scene>()->Resolve();

	// Return it as a component of the requested type
	return dynamic_cast<Type*>(component);
}

template <class Type>
Type* GameObject::GetComponent()
{
	for (auto it = m_components.begin(); it != m_components.end(); ++it)
	{
		IComponent* component = it->second;

		// casting failure == nullptr
		Type* typed_cmp = dynamic_cast<Type*>(component);
		if (typed_cmp)
			return typed_cmp;
	}

	return nullptr;
}

template <class Type>
vector<Type*> GameObject::GetComponents()
{
	vector<Type*> components;

	for (auto it = m_components.begin(); it != m_components.end(); ++it)
	{
		IComponent* component = it->second;

		// casting failure == nullptr
		Type* typed_cmp = dynamic_cast<Type*>(component);
		if (typed_cmp)
			components.push_back(typed_cmp);
	}

	return components;
}

template <class Type>
bool GameObject::HasComponent()
{
	if (GetComponent<Type>())
		return true;

	return false;
}

template <class Type>
void GameObject::RemoveComponent()
{
	for (auto it = m_components.begin(); it != m_components.end();)
	{
		IComponent* component = it->second;

		// casting failure == nullptr
		Type* typed_cmp = dynamic_cast<Type*>(component);
		if (typed_cmp)
		{
			component->Remove();
			delete component;
			it = m_components.erase(it);
			continue;
		}
		++it;
	}

	m_context->GetSubsystem<Scene>()->Resolve();
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

/*------------------------------------------------------------------------------
			[Explicit template declerations and exporting]
------------------------------------------------------------------------------*/
template __declspec(dllexport) Transform* GameObject::AddComponent<Transform>();
template __declspec(dllexport) MeshFilter* GameObject::AddComponent<MeshFilter>();
template __declspec(dllexport) MeshRenderer* GameObject::AddComponent<MeshRenderer>();
template __declspec(dllexport) Light* GameObject::AddComponent<Light>();
template __declspec(dllexport) Camera* GameObject::AddComponent<Camera>();
template __declspec(dllexport) Skybox* GameObject::AddComponent<Skybox>();
template __declspec(dllexport) RigidBody* GameObject::AddComponent<RigidBody>();
template __declspec(dllexport) Collider* GameObject::AddComponent<Collider>();
template __declspec(dllexport) MeshCollider* GameObject::AddComponent<MeshCollider>();
template __declspec(dllexport) Hinge* GameObject::AddComponent<Hinge>();
template __declspec(dllexport) Script* GameObject::AddComponent<Script>();
template __declspec(dllexport) LineRenderer* GameObject::AddComponent<LineRenderer>();

template __declspec(dllexport) Transform* GameObject::GetComponent();
template __declspec(dllexport) MeshFilter* GameObject::GetComponent();
template __declspec(dllexport) MeshRenderer* GameObject::GetComponent();
template __declspec(dllexport) Light* GameObject::GetComponent();
template __declspec(dllexport) Camera* GameObject::GetComponent();
template __declspec(dllexport) Skybox* GameObject::GetComponent();
template __declspec(dllexport) RigidBody* GameObject::GetComponent();
template __declspec(dllexport) Collider* GameObject::GetComponent();
template __declspec(dllexport) MeshCollider* GameObject::GetComponent();
template __declspec(dllexport) Hinge* GameObject::GetComponent();
template __declspec(dllexport) Script* GameObject::GetComponent();
template __declspec(dllexport) LineRenderer* GameObject::GetComponent();

template __declspec(dllexport) vector<Transform*> GameObject::GetComponents();
template __declspec(dllexport) vector<MeshFilter*> GameObject::GetComponents();
template __declspec(dllexport) vector<MeshRenderer*> GameObject::GetComponents();
template __declspec(dllexport) vector<Light*> GameObject::GetComponents();
template __declspec(dllexport) vector<Camera*> GameObject::GetComponents();
template __declspec(dllexport) vector<Skybox*> GameObject::GetComponents();
template __declspec(dllexport) vector<RigidBody*> GameObject::GetComponents();
template __declspec(dllexport) vector<Collider*> GameObject::GetComponents();
template __declspec(dllexport) vector<MeshCollider*> GameObject::GetComponents();
template __declspec(dllexport) vector<Hinge*> GameObject::GetComponents();
template __declspec(dllexport) vector<Script*> GameObject::GetComponents();
template __declspec(dllexport) vector<LineRenderer*> GameObject::GetComponents();

template __declspec(dllexport) bool GameObject::HasComponent<Transform>();
template __declspec(dllexport) bool GameObject::HasComponent<MeshFilter>();
template __declspec(dllexport) bool GameObject::HasComponent<MeshRenderer>();
template __declspec(dllexport) bool GameObject::HasComponent<Light>();
template __declspec(dllexport) bool GameObject::HasComponent<Camera>();
template __declspec(dllexport) bool GameObject::HasComponent<Skybox>();
template __declspec(dllexport) bool GameObject::HasComponent<RigidBody>();
template __declspec(dllexport) bool GameObject::HasComponent<Collider>();
template __declspec(dllexport) bool GameObject::HasComponent<MeshCollider>();
template __declspec(dllexport) bool GameObject::HasComponent<Hinge>();
template __declspec(dllexport) bool GameObject::HasComponent<Script>();
template __declspec(dllexport) bool GameObject::HasComponent<LineRenderer>();

template __declspec(dllexport) void GameObject::RemoveComponent<Transform>();
template __declspec(dllexport) void GameObject::RemoveComponent<MeshFilter>();
template __declspec(dllexport) void GameObject::RemoveComponent<MeshRenderer>();
template __declspec(dllexport) void GameObject::RemoveComponent<Light>();
template __declspec(dllexport) void GameObject::RemoveComponent<Camera>();
template __declspec(dllexport) void GameObject::RemoveComponent<Skybox>();
template __declspec(dllexport) void GameObject::RemoveComponent<RigidBody>();
template __declspec(dllexport) void GameObject::RemoveComponent<Collider>();
template __declspec(dllexport) void GameObject::RemoveComponent<MeshCollider>();
template __declspec(dllexport) void GameObject::RemoveComponent<Hinge>();
template __declspec(dllexport) void GameObject::RemoveComponent<Script>();
template __declspec(dllexport) void GameObject::RemoveComponent<LineRenderer>();
