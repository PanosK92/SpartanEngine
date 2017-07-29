/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES =======================
#include <map>
#include <vector>
#include "Scene.h"
#include "../Components/Component.h"
#include "../Core/Context.h"
//==================================

namespace Directus
{
	class Transform;

	class DLL_API GameObject
	{
	public:
		GameObject(Context* context);
		~GameObject();

		void Initialize(Transform* transform);

		void Start();
		void OnDisable();
		void Update();

		bool SaveAsPrefab(const std::string& filePath);
		bool LoadFromPrefab(const std::string& filePath);

		void Serialize();
		void Deserialize(Transform* parent);

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
			std::string typeStr = GetTypeSTR<T>();

			// Check if a component of that type already exists
			Component* existingComp = GetComponent<T>();
			if (existingComp && typeStr != "Script") // If it's anything but a script, it can't have multiple instances,
				return static_cast<T*>(existingComp); // return the existing component.

			// Get the created component.
			Component* component = new T;

			// Add the component.
			m_components.push_back(component);

			component->Register();

			// Set default properties.
			component->g_enabled = true;
			component->g_gameObject = m_context->GetSubsystem<Scene>()->GetGameObjectByID(GetID());
			component->g_transform = GetTransform();
			component->g_context = m_context;

			// Run Initialize().
			component->Reset();

			// Return it as a component of the requested type
			return static_cast<T*>(component);
		}

		// Returns a component of type T (if it exists)
		template <class T>
		T* GetComponent()
		{
			for (const auto& component : m_components)
			{
				if (typeid(T) != typeid(*component))
					continue;

				return static_cast<T*>(component);
			}

			return nullptr;
		}

		// Returns any components of type T (if they exist)
		template <class T>
		std::vector<T*> GetComponents()
		{
			std::vector<T*> components;
			for (const auto& component : m_components)
			{
				if (typeid(T) != typeid(*component))
					continue;

				components.push_back(static_cast<T*>(component));
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
			for (auto it = m_components.begin(); it != m_components.end(); )
			{
				auto component = *it;
				if (typeid(T) == typeid(*component))
				{
					delete component;
					it = m_components.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		void RemoveComponentByID(const std::string& id);
		//======================================================================================================

		Transform* GetTransform() { return m_transform; }

	private:
		std::string m_ID;
		std::string m_name;
		bool m_isActive;
		bool m_isPrefab;
		bool m_hierarchyVisibility;
		std::vector<Component*> m_components;

		// This is the only component that is guaranteed to be always attached,
		// it also is required by most systems in the engine, it's important to
		// keep a local copy of it here and avoid any runtime searching (performance).
		Transform* m_transform;

		Context* m_context;

		//= HELPER FUNCTIONS ====================================
		Component* AddComponentBasedOnType(const std::string& typeStr);

		template <class T>
		static std::string GetTypeSTR()
		{
			static std::string typeStr = typeid(T).name(); // e.g. Directus::Transform
			return typeStr.substr(typeStr.find_last_of(":") + 1); // e.g Transform
		}
	};
}