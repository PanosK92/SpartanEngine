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
#include "../Events/EventHandler.h"
//===================================

class Transform;

class DllExport GameObject
{
public:
	GameObject(Context* context);
	~GameObject();

	void Start();
	void Update();

	bool SaveAsPrefab(const std::string& filePath);
	bool LoadFromPrefab(const std::string& filePath);

	void Serialize();
	void Deserialize();

	//= PROPERTIES =========================================================================================
	std::string GetName() { return m_name; }
	void SetName(const std::string& name) { m_name = name; }

	std::string GetID() { return m_ID; }
	void SetID(const std::string& ID) { m_ID = ID; }

	bool IsActive() { return m_isActive; }
	void SetActive(bool active) { m_isActive = active; }

	bool IsVisibleInHierarchy() { return m_hierarchyVisibility; }
	void SetHierarchyVisibility(bool hierarchyVisibility) { m_hierarchyVisibility = hierarchyVisibility; }
	//======================================================================================================

	//= COMPONENTS =========================================================================================
	// Adds a component of type T
	template <class T>
	T* AddComponent()
	{
		// Convert class Type to a string.
		std::string typeStr(typeid(T).name());
		typeStr = typeStr.substr(typeStr.find_first_of(" \t") + 1); // remove word "class".

		// Check if a component of that type already exists
		IComponent* existingComp = GetComponent<T>();
		if (existingComp && typeStr != "Script") // If it's anything but a script, it can't have multiple instances,
				return dynamic_cast<T*>(existingComp); // return the existing component.

		// Get the created component.
		IComponent* component = new T;

		// Add the component.
		m_components.insert(std::pair<std::string, IComponent*>(typeStr, component));

		// Set default properties.
		component->g_ID = GENERATE_GUID;
		component->g_enabled = true;
		component->g_gameObject = this;
		component->g_transform = GetTransform();
		component->g_context = m_context;

		// Run Initialize().
		component->Initialize();

		// Return it as a component of the requested type
		return dynamic_cast<T*>(component);
	}

	// Returns a component of type T (if it exists)
	template <class T>
	T* GetComponent()
	{
		for (const auto& it : m_components)
		{
			// casting failure == nullptr
			T* typed_cmp = dynamic_cast<T*>(it.second);
			if (typed_cmp)
				return typed_cmp;
		}

		return nullptr;
	}

	// Returns any components of type T (if they exist)
	template <class T>
	std::vector<T*> GetComponents()
	{
		std::vector<T*> components;
		for (const auto& it : m_components)
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
	bool HasComponent() { return GetComponent<T>() ? true : false; }

	// Removes a component of type T (if it exists)
	template <class T>
	void RemoveComponent()
	{
		for (auto it = m_components.begin(); it != m_components.end();)
		{
			// casting failure == nullptr
			if (dynamic_cast<T*>(it->second))
			{
				it->second->Remove();
				delete it->second;
				it = m_components.erase(it);
				continue;
			}
			++it;
		}
	}

	void RemoveComponentByID(const std::string& id);
	//======================================================================================================

	Transform* GetTransform() { return m_transform; }

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
