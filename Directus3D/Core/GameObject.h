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

#pragma once

//= INCLUDES ========================
#include <map>
#include "../Components/IComponent.h"
#include <vector>
#include "../Core/Context.h"
#include "GUIDGenerator.h"
#include "Scene.h"
//===================================

class Transform;

class __declspec(dllexport) GameObject
{
public:
	GameObject();
	~GameObject();

	void Initialize(Context* context);
	void Start();
	void Update();

	std::string GetName();
	void SetName(const std::string& name);

	std::string GetID();
	void SetID(const std::string& ID);

	void SetActive(bool active);
	bool IsActive();

	void SetHierarchyVisibility(bool value);
	bool IsVisibleInHierarchy();

	void Serialize();
	void Deserialize();

	//= Components ============================================
	template <class Type>
	Type* AddComponent()
	{
		// Convert class Type to a string.
		std::string typeStr(typeid(Type).name());
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
		m_components.insert(std::pair<std::string, IComponent*>(typeStr, component));

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

	// Returns a component of type T (if it exists)
	template <class T>
	T* GetComponent()
	{
		for (auto const& it : m_components)
		{
			// casting failure == nullptr
			T* typed_cmp = dynamic_cast<T*>(it.second);
			if (dynamic_cast<T*>(it.second))
				return typed_cmp;
		}

		return nullptr;
	}

	// Returns any components of type T (if they exist)
	template <class T>
	std::vector<T*> GetComponents()
	{
		std::vector<T*> components;
		for (auto const& it : m_components)
		{
			// casting failure == nullptr
			T* typed_cmp = dynamic_cast<T*>(it.second);
			if (typed_cmp)
				components.push_back(typed_cmp);
		}

		return components;
	}

	// Checks if a component of type T exists
	template <class T>
	bool HasComponent()
	{
		if (GetComponent<T>())
			return true;

		return false;
	}

	// Removes a component of type T (if it exists)
	template <class T>
	void RemoveComponent()
	{
		for (auto it = m_components.begin(); it != m_components.end();)
		{
			IComponent* component = it->second;

			// casting failure == nullptr
			T* typed_cmp = dynamic_cast<T*>(component);
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

	void RemoveComponentByID(const std::string& id);
	//=========================================================

	Transform* GetTransform();
private:
	std::string m_ID;
	std::string m_name;
	bool m_isActive;
	bool m_hierarchyVisibility;
	std::multimap<std::string, IComponent*> m_components;

	// This is the only component that is guaranteed to be always attached,
	// it also is required by most systems in the engine, it's important to
	// keep a local copy of it here and avoid any runtime searching (performance).
	Transform* m_transform;

	Context* m_context;

	//= HELPER FUNCTIONS ====================================
	IComponent* AddComponentBasedOnType(const std::string& typeStr);
};
