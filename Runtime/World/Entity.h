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

//= INCLUDES ===================
#include <vector>
#include "World.h"
#include "../Core/Context.h"
#include "../Core/EventSystem.h"
//==============================

namespace Spartan
{
	class Context;
	class Transform;
	class Renderable;
	
    class SPARTAN_CLASS Entity : public Spartan_Object, public std::enable_shared_from_this<Entity>
    {
    public:
        Entity(Context* context, uint32_t id = 0, uint32_t transform_id = 0);
        ~Entity();

        void Clone();
        void Serialize(FileStream* stream);
        void Deserialize(FileStream* stream, Transform* parent);

        // Name
        const std::string& GetName() const      { return m_name; }
        void SetName(const std::string& name)   { m_name = name; }

        // Active
        bool IsActive() const { return m_is_active; }
        void SetActive(const bool active);

        // Hierarchy visibility
        bool IsVisibleInHierarchy() const                               { return m_hierarchy_visibility; }
        void SetHierarchyVisibility(const bool hierarchy_visibility)    { m_hierarchy_visibility = hierarchy_visibility; }

        // Checks if a component exists
        inline bool HasComponent(const ComponentType type) { return m_component_mask & (1 << static_cast<unsigned int>(type)); }

        // Checks if a component exists
        template <class T>
        inline bool HasComponent() { return HasComponent(IComponent::TypeToEnum<T>()); }

        // Returns a component
        template <class T>
        inline std::shared_ptr<T>& GetComponent()
        {
            if (HasComponent<T>())
            {
                return m_world->GetComponentManager<T>()->GetComponent(GetId());
            }

            static std::shared_ptr<T> empty;
            return empty;
        }

        // Returns components
        template <class T>
        inline std::vector<std::shared_ptr<T>> GetComponents()
        {
            if (HasComponent<T>())
            {
                return m_world->GetComponentManager<T>()->GetComponents(GetId());
            }

            return std::vector<std::shared_ptr<T>>();
        }

        // Adds a component
        template <class T>
        inline std::shared_ptr<T> AddComponent(uint32_t component_id = 0)
        {
            const ComponentType type = IComponent::TypeToEnum<T>();

            // Return component in case it already exists while ignoring Script components (they can exist multiple times)
            if (HasComponent(type) && type != ComponentType_Script)
                return GetComponent<T>();

            // Create new component
            auto new_component = std::make_shared<T>(m_context, this);
            if (component_id != 0) new_component->SetId(component_id);
            new_component->SetType(type);
            new_component->OnInitialize();

            // Add the component to the component manager
            m_id_to_type[new_component->GetId()] = type;
            m_world->GetComponentManager<T>()->AddComponent(GetId(), new_component);

            // Add component mask
            m_component_mask |= (1 << static_cast<unsigned int>(type));

            // Caching of rendering performance critical components
            if constexpr (std::is_same<T, Renderable>::value)
            {
                m_renderable = static_cast<Renderable*>(new_component.get());
            }

            // Make the scene resolve
            FIRE_EVENT(Event_World_Resolve);

            return new_component;
        }

        // Adds a component 
        std::shared_ptr<IComponent> AddComponent(ComponentType type, uint32_t component_id = 0);

		// Removes a component
		template <class T>
        inline void RemoveComponent()
		{
            if (!HasComponent<T>())
                return;

            // Remove from component manager
            m_world->GetComponentManager<T>()->RemoveComponent(GetId());

            // Remove component mask
            const ComponentType type = IComponent::TypeToEnum<T>();
            m_component_mask &= ~(1 << static_cast<unsigned int>(type));

			// Make the scene resolve
			FIRE_EVENT(Event_World_Resolve);
		}

        // Removes a component
		void RemoveComponent(uint32_t id);

        // Misc
        std::vector<std::shared_ptr<IComponent>> GetAllComponents() const;
		Transform* GetTransform_PtrRaw() const		{ return m_transform; }
		Renderable* GetRenderable_PtrRaw() const	{ return m_renderable; }
		std::shared_ptr<Entity> GetPtrShared()		{ return shared_from_this(); }

	private:
		std::string m_name			= "Entity";
		bool m_is_active			= true;
		bool m_hierarchy_visibility	= true;

        // Component management
        unsigned int m_component_mask = 0;
        std::unordered_map<uint32_t, ComponentType> m_id_to_type;

        // Caching of performance critical components
        Transform* m_transform      = nullptr;
        Renderable* m_renderable    = nullptr;
        Context* m_context          = nullptr;
        World* m_world              = nullptr;
	};
}
