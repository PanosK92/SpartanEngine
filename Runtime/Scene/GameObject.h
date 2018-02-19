/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES =====================
#include <map>
#include "Scene.h"
#include "Components/IComponent.h"
#include "../Core/Context.h"
//================================

namespace Directus
{
	class Transform;
	class MeshFilter;
	class MeshRenderer;

	class ENGINE_CLASS GameObject
	{
	public:
		GameObject(Context* context);
		~GameObject();

		void Initialize(Transform* transform);

		//============
		void Start();
		void Stop();
		void Update();
		//============

		bool SaveAsPrefab(const std::string& filePath);
		bool LoadFromPrefab(const std::string& filePath);

		void Serialize(FileStream* stream);
		void Deserialize(FileStream* stream, Transform* parent);

		//= PROPERTIES =======================================================================================
		const std::string& GetName() { return m_name; }
		void SetName(const std::string& name) { m_name = name; }

		unsigned int GetID() { return m_ID; }
		void SetID(unsigned int ID) { m_ID = ID; }

		bool IsActive() { return m_isActive; }
		void SetActive(bool active) { m_isActive = active; }

		bool IsVisibleInHierarchy() { return m_hierarchyVisibility; }
		void SetHierarchyVisibility(bool hierarchyVisibility) { m_hierarchyVisibility = hierarchyVisibility; }
		//====================================================================================================

		//= COMPONENTS =========================================================================================
		// Adds a component of type T
		template <class T>
		std::weak_ptr<T> AddComponent()
		{
			ComponentType type = IComponent::ToComponentType<T>();

			// Return component in case it already exists while ignoring Script components (they can exist multiple times)
			if (HasComponent(type) && type != ComponentType_Script)
				return std::static_pointer_cast<T>(GetComponent<T>().lock());

			// Add component
			auto newComponent = std::make_shared<T>(
				m_context, 
				m_context->GetSubsystem<Scene>()->GetGameObjectByID(GetID()).lock().get(),
				GetTransform()
				);
			m_components.insert(make_pair(type, newComponent));
			newComponent->SetType(IComponent::ToComponentType<T>());

			// Register component
			newComponent->Initialize();

			// Caching of rendering performance critical components
			if (newComponent->GetType() == ComponentType_MeshFilter)
			{
				m_meshFilter = (MeshFilter*)newComponent.get();
			}
			else if (newComponent->GetType() == ComponentType_MeshRenderer)
			{
				m_meshRenderer = (MeshRenderer*)newComponent.get();
			}

			// Return it as a component of the requested type
			return newComponent;
		}

		std::weak_ptr<IComponent> AddComponent(ComponentType type);

		// Returns a component of type T (if it exists)
		template <class T>
		std::weak_ptr<T> GetComponent()
		{
			ComponentType type = IComponent::ToComponentType<T>();

			if (m_components.find(type) == m_components.end())
				return std::weak_ptr<T>();

			return std::static_pointer_cast<T>(m_components.find(type)->second);
		}

		// Returns any components of type T (if they exist)
		template <class T>
		std::vector<std::weak_ptr<T>> GetComponents()
		{
			ComponentType type = IComponent::ToComponentType<T>();

			std::vector<std::weak_ptr<T>> components;
			for (const auto& component : m_components)
			{
				if (type != component.second->GetType())
					continue;

				components.push_back(std::static_pointer_cast<T>(component.second));
			}

			return components;
		}
		
		// Checks if a component of ComponentType exists
		bool HasComponent(ComponentType type) { return m_components.find(type) != m_components.end(); }
		// Checks if a component of type T exists
		template <class T>
		bool HasComponent() { return HasComponent(IComponent::ToComponentType<T>()); }

		// Removes a component of type T (if it exists)
		template <class T>
		void RemoveComponent()
		{
			ComponentType type = IComponent::ToComponentType<T>();

			if (m_components.find(type) == m_components.end())
				return;

			for (auto it = m_components.begin(); it != m_components.end(); )
			{
				auto component = *it;
				if (type == component.second->GetType())
				{
					component.second->Remove();
					component.second.reset();
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
		std::multimap<ComponentType, std::shared_ptr<IComponent>> m_components;
		Context* m_context;

		// Caching of performance critical components
		Transform* m_transform; // Updating performance - never null
		MeshFilter* m_meshFilter; // Rendering performance - can be null
		MeshRenderer* m_meshRenderer; // Rendering performance - can be null
	};
}
