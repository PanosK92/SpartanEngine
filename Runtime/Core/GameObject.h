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
#include <vector>
#include "Scene.h"
#include "../Components/Component.h"
#include "../Core/Context.h"
//==================================

namespace Directus
{
	class Transform;
	class MeshFilter;
	class MeshRenderer;

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

		void Serialize(StreamIO* stream);
		void Deserialize(StreamIO* stream, Transform* parent);

		//= PROPERTIES =========================================================================================
		const std::string& GetName() { return m_name; }
		void SetName(const std::string& name) { m_name = name; }

		unsigned int GetID() { return m_ID; }
		void SetID(unsigned int ID) { m_ID = ID; }

		bool IsActive() { return m_isActive; }
		void SetActive(bool active) { m_isActive = active; }

		bool IsVisibleInHierarchy() { return m_hierarchyVisibility; }
		void SetHierarchyVisibility(bool hierarchyVisibility) { m_hierarchyVisibility = hierarchyVisibility; }
		//======================================================================================================

		//= COMPONENTS =========================================================================================
		// Adds a component of type T
		template <class T>
		std::weak_ptr<T> AddComponent()
		{
			std::string typeStr = GetTypeSTR<T>();

			// Return existing component but allow multiple Script components
			std::weak_ptr<Component> existingComp = GetComponent<T>();
			if (!existingComp.expired() && typeStr != "Script")
				return std::static_pointer_cast<T>(existingComp.lock());

			// Get the created component.
			std::shared_ptr<Component> component = std::make_shared<T>();

			// Add the component.
			m_components.push_back(component);

			component->Register();

			// Set default properties.
			component->g_enabled = true;
			component->g_gameObject = m_context->GetSubsystem<Scene>()->GetGameObjectByID(GetID());
			component->g_transform = GetTransform();
			component->g_context = m_context;

			// Run Initialize().
			component->Initialize();

			// Caching of rendering performance critical components
			if (typeStr == "MeshFilter")
			{
				m_meshFilter = (MeshFilter*)component.get();
			}
			else if (typeStr == "MeshRenderer")
			{
				m_meshRenderer = (MeshRenderer*)component.get();
			}

			// Return it as a component of the requested type
			return ToDerivedWeak<T>(component);
		}

		// Returns a component of type T (if it exists)
		template <class T>
		std::weak_ptr<T> GetComponent()
		{
			for (const auto& component : m_components)
			{
				if (typeid(T) != typeid(*component.get()))
					continue;

				return ToDerivedWeak<T>(component);
			}

			return std::weak_ptr<T>();
		}

		// Returns any components of type T (if they exist)
		template <class T>
		std::vector<std::weak_ptr<T>> GetComponents()
		{
			std::vector<std::weak_ptr<T>> components;
			for (const auto& component : m_components)
			{
				if (typeid(T) != typeid(*component.get()))
					continue;

				components.push_back(ToDerivedWeak<T>(component));
			}

			return components;
		}

		// Checks if a component of type T exists
		template <class T>
		bool HasComponent() { return !GetComponent<T>().expired(); }

		// Removes a component of type T (if it exists)
		template <class T>
		void RemoveComponent()
		{
			for (auto it = m_components.begin(); it != m_components.end(); )
			{
				auto component = *it;
				if (typeid(T) == typeid(*component.get()))
				{
					component->Remove();
					component.reset();
					it = m_components.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		void RemoveComponentByID(unsigned int id);
		//======================================================================================================

		// Direct access to performance critical components (not safe)
		Transform* GetTransform() { return m_transform; }
		MeshFilter* GetMeshFilter() { return m_meshFilter; }
		MeshRenderer* GetMeshRenderer() { return m_meshRenderer; }

	private:
		unsigned int m_ID;
		std::string m_name;
		bool m_isActive;
		bool m_isPrefab;
		bool m_hierarchyVisibility;
		std::vector<std::shared_ptr<Component>> m_components;

		// Caching of performance critical components
		Transform* m_transform; // Updating performance - never null
		MeshFilter* m_meshFilter; // Rendering performance - can be null
		MeshRenderer* m_meshRenderer; // Rendering performance - can be null

		// Dependencies
		Context* m_context;

		//= HELPER FUNCTIONS ====================================
		std::weak_ptr<Component> AddComponentBasedOnType(const std::string& typeStr);

		template <class T>
		static std::string GetTypeSTR()
		{
			static std::string typeStr = typeid(T).name(); // e.g. Directus::Transform
			return typeStr.substr(typeStr.find_last_of(":") + 1); // e.g Transform
		}

		template <class T>
		static std::weak_ptr<T> ToDerivedWeak(std::shared_ptr<Component> base)
		{
			std::shared_ptr<T> derivedShared = std::static_pointer_cast<T>(base);
			std::weak_ptr<T> derivedWeak = std::weak_ptr<T>(derivedShared);

			return derivedWeak;
		}
	};
}