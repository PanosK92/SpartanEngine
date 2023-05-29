/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ====================
#include "Components/Component.h"
#include "Event.h"
//===============================

namespace Spartan
{
    class Transform;
    class Renderable;
    
    class SP_CLASS Entity : public Object, public std::enable_shared_from_this<Entity>
    {
    public:
        Entity();
        ~Entity();

        void Initialize();
        void Clone();

        // Runs once, before the simulation ends.
        void OnStart();

        // Runs once, after the simulation ends.
        void OnStop();

        // Runs every frame, before any subsystem or entity ticks.
        void OnPreTick();

        // Runs every frame.
        void Tick();

        void Serialize(FileStream* stream);
        void Deserialize(FileStream* stream, std::shared_ptr<Transform> parent);

        // Active
        bool IsActive() const             { return m_is_active; }
        void SetActive(const bool active) { m_is_active = active; }
        bool IsActiveRecursively();

        // Visible
        bool IsVisibleInHierarchy() const                            { return m_hierarchy_visibility; }
        void SetHierarchyVisibility(const bool hierarchy_visibility) { m_hierarchy_visibility = hierarchy_visibility; }

        // Adds a component of type T
        template <class T>
        std::shared_ptr<T> AddComponent()
        {
            const ComponentType type = Component::TypeToEnum<T>();

            // Early exit if the component exists
            if (std::shared_ptr<T> component = GetComponent<T>())
                return component;

            // Create a new component
            std::shared_ptr<T> component = std::make_shared<T>(this->shared_from_this());

            // Save new component
            m_components[static_cast<uint32_t>(type)] = std::static_pointer_cast<Component>(component);

            // Initialize component
            component->SetType(type);
            component->OnInitialize();

            // Make the scene resolve
            SP_FIRE_EVENT(EventType::WorldResolve);

            return component;
        }

        // Adds a component of ComponentType 
        std::shared_ptr<Component> AddComponent(ComponentType type);

        // Returns a component of type T
        template <class T>
        std::shared_ptr<T> GetComponent()
        {
            const ComponentType component_type = Component::TypeToEnum<T>();
            return std::static_pointer_cast<T>(m_components[static_cast<uint32_t>(component_type)]);
        }

        // Removes a component
        template <class T>
        void RemoveComponent()
        {
            const ComponentType component_type = Component::TypeToEnum<T>();
            m_components[static_cast<uint32_t>(component_type)] = nullptr;

            SP_FIRE_EVENT(EventType::WorldResolve);
        }

        void RemoveComponentById(uint64_t id);
        const auto& GetAllComponents() const { return m_components; }
        std::shared_ptr<Transform> GetTransform();

    private:
        std::atomic<bool> m_is_active = true;
        bool m_hierarchy_visibility   = true;
        std::array<std::shared_ptr<Component>, 14> m_components;
    };
}
