/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include <vector>
#include "World.h"
#include "Components/IComponent.h"
#include "../Core/Context.h"
#include "../Core/EventSystem.h"
//================================

namespace Directus
{
	class Transform;
	class Renderable;
	#define ValidateComponentType(T) static_assert(std::is_base_of<IComponent, T>::value, "Provided type does not implement IComponent")

	class ENGINE_CLASS Entity : public std::enable_shared_from_this<Entity>
	{
	public:
		Entity(Context* context);
		~Entity();

		void Initialize(Transform* transform);
		void Clone();

		//============
		void Start();
		void Stop();
		void Tick();
		//============

		void Serialize(FileStream* stream);
		void Deserialize(FileStream* stream, Transform* parent);

		//= PROPERTIES =========================================================================================
		const std::string& GetName()			{ return m_name; }
		void SetName(const std::string& name)	{ m_name = name; }

		unsigned int GetID()		{ return m_ID; }
		void SetID(unsigned int ID) { m_ID = ID; }

		bool IsActive()				{ return m_isActive; }
		void SetActive(bool active) { m_isActive = active; }

		bool IsVisibleInHierarchy()								{ return m_hierarchyVisibility; }
		void SetHierarchyVisibility(bool hierarchyVisibility)	{ m_hierarchyVisibility = hierarchyVisibility; }
		//======================================================================================================

		//= COMPONENTS =========================================================================================
		// Adds a component of type T
		template <class T>
		std::shared_ptr<T> AddComponent()
		{
			ValidateComponentType(T);
			ComponentType type = IComponent::Type_To_Enum<T>();

			// Return component in case it already exists while ignoring Script components (they can exist multiple times)
			if (HasComponent(type) && type != ComponentType_Script)
				return GetComponent<T>();

			// Add component
			m_components.emplace_back
			(	
				std::make_shared<T>
				(
					m_context,
					this,
					GetTransform_PtrRaw()
				)
			);

			auto newComponent = std::static_pointer_cast<T>(m_components.back());
			newComponent->SetType(IComponent::Type_To_Enum<T>());
			newComponent->OnInitialize();

			// Caching of rendering performance critical components
			if (newComponent->GetType() == ComponentType_Renderable)
			{
				m_renderable = (Renderable*)newComponent.get();
			}

			// Make the scene resolve
			FIRE_EVENT(Event_World_Resolve);

			return newComponent;
		}

		std::shared_ptr<IComponent> AddComponent(ComponentType type);

		// Returns a component of type T (if it exists)
		template <class T>
		std::shared_ptr<T> GetComponent()
		{
			ValidateComponentType(T);
			ComponentType type = IComponent::Type_To_Enum<T>();

			for (const auto& component : m_components)
			{
				if (component->GetType() == type)
					return std::static_pointer_cast<T>(component);
			}

			return nullptr;
		}

		// Returns any components of type T (if they exist)
		template <class T>
		std::vector<std::shared_ptr<T>> GetComponents()
		{
			ValidateComponentType(T);
			ComponentType type = IComponent::Type_To_Enum<T>();

			std::vector<std::shared_ptr<T>> components;
			for (const auto& component : m_components)
			{
				if (component->GetType() != type)
					continue;

				components.emplace_back(std::static_pointer_cast<T>(component));
			}

			return components;
		}
		
		// Checks if a component of ComponentType exists
		bool HasComponent(ComponentType type) 
		{ 
			for (const auto& component : m_components)
			{
				if (component->GetType() == type)
					return true;
			}

			return false;
		}

		// Checks if a component of type T exists
		template <class T>
		bool HasComponent() 
		{ 
			ValidateComponentType(T);
			return HasComponent(IComponent::Type_To_Enum<T>()); 
		}

		// Removes a component of type T (if it exists)
		template <class T>
		void RemoveComponent()
		{
			ValidateComponentType(T);
			ComponentType type = IComponent::Type_To_Enum<T>();

			for (auto it = m_components.begin(); it != m_components.end();)
			{
				auto component = *it;
				if (component->GetType() == type)
				{
					component->OnRemove();
					component.reset();
					it = m_components.erase(it);
				}
				else
				{
					++it;
				}
			}

			// Make the scene resolve
			FIRE_EVENT(Event_World_Resolve);
		}

		void RemoveComponentByID(unsigned int id);

		const auto& GetAllComponents() { return m_components; }
		//======================================================================================================

		// Direct access for performance critical usage (not safe)
		Transform* GetTransform_PtrRaw()		{ return m_transform; }
		Renderable* GetRenderable_PtrRaw()		{ return m_renderable; }
		std::shared_ptr<Entity> GetPtrShared()	{ return shared_from_this(); }

	private:
		unsigned int m_ID			= 0;
		std::string m_name			= "Entity";
		bool m_isActive				= true;
		bool m_hierarchyVisibility	= true;
		// Caching of performance critical components
		Transform* m_transform		= nullptr;
		Renderable* m_renderable	= nullptr;

		// Components
		std::vector<std::shared_ptr<IComponent>> m_components;
		std::shared_ptr<Entity> m_componentEmpty;

		// Misc
		Context* m_context;
	};
}
