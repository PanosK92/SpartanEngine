/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include <atomic>
#include <array>
#include <mutex>
#include "World.h"
#include "Components/Component.h"
#include "../Math/Quaternion.h"
#include "../Math/Matrix.h"
//===============================

namespace pugi
{
    class xml_node;
}

namespace spartan
{
    class Renderable;

    class Entity : public SpartanObject
    {
    public:
        Entity();
        ~Entity();

        Entity* Clone();

        // core
        void OnStart();
        void OnStop();
        void PreTick();
        void Tick();

        // io
        void Save(pugi::xml_node& node);
        void Load(pugi::xml_node& node);

        // active
        bool GetActive();
        void SetActive(const bool active);

        // adds a component of type T
        template <class T>
        T* AddComponent()
        {
            const ComponentType type = Component::TypeToEnum<T>();

            // early exit if the component exists
            if (T* component = GetComponent<T>())
                return component;

            // create a new component
            std::shared_ptr<T> component = std::make_shared<T>(this);

            // save new component
            m_components[static_cast<uint32_t>(type)] = std::static_pointer_cast<Component>(component);

            // initialize component
            component->SetType(type);
            component->OnInitialize();

            return component.get();
        }

        // adds a component of ComponentType 
        Component* AddComponent(ComponentType type);

        // returns a component of type T
        template <class T>
        T* GetComponent()
        {
            const ComponentType component_type = Component::TypeToEnum<T>();
            return static_cast<T*>(m_components[static_cast<uint32_t>(component_type)].get());
        }

        // removes a component
        template <class T>
        void RemoveComponent()
        {
            const ComponentType component_type = Component::TypeToEnum<T>();
            m_components[static_cast<uint32_t>(component_type)] = nullptr;

            World::Resolve();
        }

        void RemoveComponentById(uint64_t id);
        const auto& GetAllComponents() const { return m_components; }
        uint32_t GetComponentCount() const;

        //= POSITION ======================================================================
        math::Vector3 GetPosition()             const { return m_matrix.GetTranslation(); }
        const math::Vector3& GetPositionLocal() const { return m_position_local; }
        void SetPosition(const math::Vector3& position);
        void SetPositionLocal(const math::Vector3& position);
        //=================================================================================

        //= ROTATION ======================================================================
        math::Quaternion GetRotation()             const { return m_matrix.GetRotation(); }
        const math::Quaternion& GetRotationLocal() const { return m_rotation_local; }
        void SetRotation(const math::Quaternion& rotation);
        void SetRotationLocal(const math::Quaternion& rotation);
        //=================================================================================

        //= SCALE ================================================================
        math::Vector3 GetScale()             const { return m_matrix.GetScale(); }
        const math::Vector3& GetScaleLocal() const { return m_scale_local; }
        void SetScale(const math::Vector3& scale);
        void SetScaleLocal(const math::Vector3& scale);
        //========================================================================

        //= TRANSLATION/ROTATION ==================
        void Translate(const math::Vector3& delta);
        void Rotate(const math::Quaternion& delta);
        //=========================================

        //= DIRECTIONS ================================================
        const math::Vector3& GetUp() const       { return m_up; }
        const math::Vector3& GetDown() const     { return m_down; }
        const math::Vector3& GetForward() const  { return m_forward; }
        const math::Vector3& GetBackward() const { return m_backward; }
        const math::Vector3& GetRight() const    { return m_right; }
        const math::Vector3& GetLeft() const     { return m_left; }
        //=============================================================

        //= HIERARCHY ===================================================================================
        void SetParent(Entity* new_parent);
        Entity* GetChildByIndex(uint32_t index);
        Entity* GetChildByName(const std::string& name);
        void AcquireChildren();
        void RemoveChild(Entity* child, bool update_child_with_null_parent = true);
        void AddChild(Entity* child);
        bool IsDescendantOf(Entity* transform) const;
        void GetDescendants(std::vector<Entity*>* descendants);
        Entity* GetDescendantByName(const std::string& name);
        bool HasChildren() const                  { return GetChildrenCount() > 0; }
        uint32_t GetChildrenCount() const         { return static_cast<uint32_t>(m_children.size()); }
        Entity* GetRoot()                         { return m_parent ? GetParent()->GetRoot() : this; }
        Entity* GetParent()                       { return m_parent; }
        std::vector<Entity*>& GetChildren()       { return m_children; }
        //===============================================================================================

        const math::Matrix& GetMatrix() const              { return m_matrix; }
        const math::Matrix& GetLocalMatrix() const         { return m_matrix_local; }
        const math::Matrix& GetMatrixPrevious() const      { return m_matrix_previous; }
        void SetMatrixPrevious(const math::Matrix& matrix) { m_matrix_previous = matrix; }
        float GetTimeSinceLastTransform() const            { return m_time_since_last_transform_sec; }

    private:
        std::atomic<bool> m_is_active = true;
        std::array<std::shared_ptr<Component>, static_cast<uint32_t>(ComponentType::Max)> m_components;

        void UpdateTransform();
        math::Matrix GetParentTransformMatrix();

        // local
        math::Vector3 m_position_local    = math::Vector3::Zero;
        math::Quaternion m_rotation_local = math::Quaternion::Identity;
        math::Vector3 m_scale_local       = math::Vector3::One;

        math::Matrix m_matrix          = math::Matrix::Identity;
        math::Matrix m_matrix_previous = math::Matrix::Identity;
        math::Matrix m_matrix_local    = math::Matrix::Identity;

        // computed during UpdateTransform() and cached for performance
        math::Vector3 m_forward  = math::Vector3::Zero;
        math::Vector3 m_backward = math::Vector3::Zero;
        math::Vector3 m_up       = math::Vector3::Zero;
        math::Vector3 m_down     = math::Vector3::Zero;
        math::Vector3 m_right    = math::Vector3::Zero;
        math::Vector3 m_left     = math::Vector3::Zero;

        Entity* m_parent = nullptr;      // the parent of this entity
        std::vector<Entity*> m_children; // the children of this entity

        // misc
        std::mutex m_mutex_children;
        std::mutex m_mutex_parent;
        float m_time_since_last_transform_sec = 0.0f;
    };
}
