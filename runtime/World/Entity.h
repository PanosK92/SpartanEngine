/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "../Core/Events.h"
#include "Components/IComponent.h"
//================================

namespace Spartan
{
    class Context;
    class Transform;
    class Renderable;
    
    class SP_CLASS Entity : public SpartanObject, public std::enable_shared_from_this<Entity>
    {
    public:
        Entity(Context* context, uint64_t transform_id = 0);
        ~Entity();

        void Clone();

        // Runs once, before the simulation ends.
        void OnStart();

        // Runs once, after the simulation ends.
        void OnStop();

        // Runs every frame, before any subsystem or entity ticks.
        void OnPreTick();

        // Runs every frame.
        void Tick(double delta_time);

        void Serialize(FileStream* stream);
        void Deserialize(FileStream* stream, Transform* parent);

        // Active
        bool IsActive() const             { return m_is_active; }
        void SetActive(const bool active) { m_is_active = active; }

        // Visible
        bool IsVisibleInHierarchy() const                            { return m_hierarchy_visibility; }
        void SetHierarchyVisibility(const bool hierarchy_visibility) { m_hierarchy_visibility = hierarchy_visibility; }

        // Adds a component of type T
        template <class T>
        T* AddComponent(uint64_t id = 0)
        {
            const ComponentType type = IComponent::TypeToEnum<T>();

            // If the component exists, return the existing one
            if (T* component = GetComponent<T>())
            {
                return component;
            }

            // Create a new component
            std::shared_ptr<T> component = std::make_shared<T>(m_context, this, id);

            // Save new component
            m_components[static_cast<uint32_t>(type)] = std::static_pointer_cast<IComponent>(component);

            // Caching of rendering performance critical components
            if constexpr (std::is_same<T, Transform>::value)  { m_transform  = static_cast<Transform*>(component.get()); }
            if constexpr (std::is_same<T, Renderable>::value) { m_renderable = static_cast<Renderable*>(component.get()); }

            // Initialize component
            component->SetType(type);
            component->OnInitialize();

            // Make the scene resolve
            SP_FIRE_EVENT(EventType::WorldResolve);

            return component.get();
        }

        // Adds a component of ComponentType 
        IComponent* AddComponent(ComponentType type, uint64_t id = 0);

        // Returns a component of type T (if it exists)
        template <class T>
        T* GetComponent()
        {
            const ComponentType component_type = IComponent::TypeToEnum<T>();
            return static_cast<T*>(m_components[static_cast<uint32_t>(component_type)].get());
        }

        // Removes a component (if it exists)
        template <class T>
        void RemoveComponent()
        {
            const ComponentType component_type = IComponent::TypeToEnum<T>();
            m_components[static_cast<uint32_t>(component_type)] = nullptr;

            SP_FIRE_EVENT(EventType::WorldResolve);
        }

        void RemoveComponentById(uint64_t id);
        const auto& GetAllComponents() const { return m_components; }

        void MarkForDestruction()         { m_destruction_pending = true; }
        bool IsPendingDestruction() const { return m_destruction_pending; }

        // Direct access for performance critical usage (not safe)
        Transform* GetTransform() const        { return m_transform; }
        Renderable* GetRenderable() const      { return m_renderable; }
        std::shared_ptr<Entity> GetPtrShared() { return shared_from_this(); }

    private:
        std::atomic<bool> m_is_active = true;
        bool m_hierarchy_visibility   = true;
        Transform* m_transform        = nullptr;
        Renderable* m_renderable      = nullptr;
        bool m_destruction_pending    = false;
        std::array<std::shared_ptr<IComponent>, 14> m_components;
    };
}
