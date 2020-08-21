/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "../Core/EventSystem.h"
#include "Components/IComponent.h"
//================================

namespace Spartan
{
    class Context;
    class Transform;
    class Renderable;
    
    class SPARTAN_CLASS Entity : public Spartan_Object, public std::enable_shared_from_this<Entity>
    {
    public:
        Entity(Context* context, uint32_t transform_id = 0);
        ~Entity();

        void Clone();
        void Start();
        void Stop();
        void Tick(float delta_time);
        void Serialize(FileStream* stream);
        void Deserialize(FileStream* stream, Transform* parent);

        //= PROPERTIES ===================================================================================================
        const std::string& GetName() const                                { return m_name; }
        void SetName(const std::string& name)                            { m_name = name; }

        bool IsActive() const                                            { return m_is_active; }
        void SetActive(const bool active)                                { m_is_active = active; }

        bool IsVisibleInHierarchy() const                                { return m_hierarchy_visibility; }
        void SetHierarchyVisibility(const bool hierarchy_visibility)    { m_hierarchy_visibility = hierarchy_visibility; }
        //================================================================================================================

        // Adds a component of type T
        template <class T>
        T* AddComponent(uint32_t id = 0)
        {
            const ComponentType type = IComponent::TypeToEnum<T>();

            // Return component in case it already exists while ignoring Script components (they can exist multiple times)
            if (HasComponent(type) && type != ComponentType::Script)
                return GetComponent<T>();

            // Create a new component
            std::shared_ptr<T> component = std::make_shared<T>(m_context, this, id);

            // Save new component
            m_components.emplace_back(std::static_pointer_cast<IComponent>(component));
            m_component_mask |= GetComponentMask(type);

            // Caching of rendering performance critical components
            if constexpr (std::is_same<T, Transform>::value)    { m_transform   = static_cast<Transform*>(component.get()); }
            if constexpr (std::is_same<T, Renderable>::value)   { m_renderable  = static_cast<Renderable*>(component.get()); }

            // Initialize component
            component->SetType(type);
            component->OnInitialize();

            // Make the scene resolve
            FIRE_EVENT(EventType::WorldResolve);

            return component.get();
        }

        // Adds a component of ComponentType 
        IComponent* AddComponent(ComponentType type, uint32_t id = 0);

        // Returns a component of type T (if it exists)
        template <class T>
        T* GetComponent()
        {
            const ComponentType type = IComponent::TypeToEnum<T>();

            if (!HasComponent(type))
                return nullptr;

            for (const auto& component : m_components)
            {
                if (component->GetType() == type)
                    return static_cast<T*>(component.get());
            }

            return nullptr;
        }

        // Returns any components of type T (if they exist)
        template <class T>
        std::vector<T*> GetComponents()
        {
            std::vector<T*> components;
            const ComponentType type = IComponent::TypeToEnum<T>();

            if (!HasComponent(type))
                return components;
        
            for (const auto& component : m_components)
            {
                if (component->GetType() != type)
                    continue;

                components.emplace_back(static_cast<T*>(component.get()));
            }

            return components;
        }
        
        // Checks if a component exists
        constexpr bool HasComponent(const ComponentType type) { return m_component_mask & GetComponentMask(type); }

        // Checks if a component exists
        template <class T>
        bool HasComponent() { return HasComponent(IComponent::TypeToEnum<T>()); }

        // Removes a component (if it exists)
        template <class T>
        void RemoveComponent()
        {
            const ComponentType type = IComponent::TypeToEnum<T>();

            for (auto it = m_components.begin(); it != m_components.end();)
            {
                auto component = *it;
                if (component->GetType() == type)
                {
                    component->OnRemove();
                    it = m_components.erase(it);
                    m_component_mask &= ~GetComponentMask(type);
                }
                else
                {
                    ++it;
                }
            }

            // Make the scene resolve
            FIRE_EVENT(Event_World_Resolve_Pending);
        }

        void RemoveComponentById(uint32_t id);
        const auto& GetAllComponents() const { return m_components; }

        void MarkForDestruction()           { m_destruction_pending = true; }
        bool IsPendingDestruction() const   { return m_destruction_pending; }

        // Direct access for performance critical usage (not safe)
        Transform* GetTransform() const            { return m_transform; }
        Renderable* GetRenderable() const        { return m_renderable; }
        std::shared_ptr<Entity> GetPtrShared()  { return shared_from_this(); }

    private:
        constexpr uint32_t GetComponentMask(ComponentType type) { return static_cast<uint32_t>(1) << static_cast<uint32_t>(type); }

        std::string m_name            = "Entity";
        bool m_is_active            = true;
        bool m_hierarchy_visibility    = true;
        Transform* m_transform        = nullptr;
        Renderable* m_renderable    = nullptr;
        bool m_destruction_pending  = false;
        
        // Components
        std::vector<std::shared_ptr<IComponent>> m_components;
        uint32_t m_component_mask = 0;
    };
}
