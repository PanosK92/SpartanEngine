/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "../Math/Quaternion.h"
#include <array>
#include "../Math/Matrix.h"
#include <mutex>
#include "Event.h"
#include "World.h"
//===============================

namespace Spartan
{
    class FileStream;
    class Renderable;
    
    class SP_CLASS Entity : public SpartanObject, public std::enable_shared_from_this<Entity>
    {
    public:
        Entity();
        ~Entity();

        void Initialize();
        std::shared_ptr<Entity> Clone();

        // core
        void OnStart(); // runs once, before the simulation ends
        void OnStop();  // runs once, after the simulation ends
        void Tick();    // runs every frame

        // io
        void Serialize(FileStream* stream);
        void Deserialize(FileStream* stream, std::shared_ptr<Entity> parent);

        // active
        bool IsActive() const;
        void SetActive(const bool active) { m_is_active = active; }

        // visible
        bool IsVisibleInHierarchy() const                            { return m_hierarchy_visibility; }
        void SetHierarchyVisibility(const bool hierarchy_visibility) { m_hierarchy_visibility = hierarchy_visibility; }

        // adds a component of type T
        template <class T>
        std::shared_ptr<T> AddComponent()
        {
            const ComponentType type = Component::TypeToEnum<T>();

            // early exit if the component exists
            if (std::shared_ptr<T> component = GetComponent<T>())
                return component;

            // create a new component
            std::shared_ptr<T> component = std::make_shared<T>(this->shared_from_this());

            // save new component
            m_components[static_cast<uint32_t>(type)] = std::static_pointer_cast<Component>(component);

            // initialize component
            component->SetType(type);
            component->OnInitialize();

            World::Resolve();

            return component;
        }

        // adds a component of ComponentType 
        std::shared_ptr<Component> AddComponent(ComponentType type);

        // returns a component of type T
        template <class T>
        std::shared_ptr<T> GetComponent()
        {
            const ComponentType component_type = Component::TypeToEnum<T>();
            return std::static_pointer_cast<T>(m_components[static_cast<uint32_t>(component_type)]);
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

        //= POSITION ======================================================================
        Math::Vector3 GetPosition()             const { return m_matrix.GetTranslation(); }
        const Math::Vector3& GetPositionLocal() const { return m_position_local; }
        void SetPosition(const Math::Vector3& position);
        void SetPositionLocal(const Math::Vector3& position);
        //=================================================================================

        //= ROTATION ======================================================================
        Math::Quaternion GetRotation()             const { return m_matrix.GetRotation(); }
        const Math::Quaternion& GetRotationLocal() const { return m_rotation_local; }
        void SetRotation(const Math::Quaternion& rotation);
        void SetRotationLocal(const Math::Quaternion& rotation);
        //=================================================================================

        //= SCALE ================================================================
        Math::Vector3 GetScale()             const { return m_matrix.GetScale(); }
        const Math::Vector3& GetScaleLocal() const { return m_scale_local; }
        void SetScale(const Math::Vector3& scale);
        void SetScaleLocal(const Math::Vector3& scale);
        //========================================================================

        //= TRANSLATION/ROTATION ==================
        void Translate(const Math::Vector3& delta);
        void Rotate(const Math::Quaternion& delta);
        //=========================================

        //= DIRECTIONS ===================
        Math::Vector3 GetUp() const;
        Math::Vector3 GetDown() const;
        Math::Vector3 GetForward() const;
        Math::Vector3 GetBackward() const;
        Math::Vector3 GetRight() const;
        Math::Vector3 GetLeft() const;
        //================================

        //= HIERARCHY ===================================================================================
        void SetParent(std::weak_ptr<Entity> new_parent);
        Entity* GetChildByIndex(uint32_t index);
        Entity* GetChildByName(const std::string& name);
        void AcquireChildren();
        void RemoveChild(Entity* child, bool update_child_with_null_parent = true);
        void AddChild(Entity* child);
        bool IsDescendantOf(Entity* transform) const;
        void GetDescendants(std::vector<Entity*>* descendants);
        Entity* GetDescendantByName(const std::string& name);
        bool HasParent() const                    { return !m_parent.expired(); }
        bool HasChildren() const                  { return GetChildrenCount() > 0 ? true : false; }
        uint32_t GetChildrenCount() const         { return static_cast<uint32_t>(m_children.size()); }
        Entity* GetRoot()                         { return HasParent() ? GetParent()->GetRoot() : this; }
        std::shared_ptr<Entity> GetParent() const { return m_parent.lock(); }
        std::vector<Entity*>& GetChildren()       { return m_children; }
        //===============================================================================================

        const Math::Matrix& GetMatrix() const              { return m_matrix; }
        const Math::Matrix& GetLocalMatrix() const         { return m_matrix_local; }
        const Math::Matrix& GetMatrixPrevious() const      { return m_matrix_previous; }
        void SetMatrixPrevious(const Math::Matrix& matrix) { m_matrix_previous = matrix; }
        bool HasTransformChanged() const;

    private:
        std::atomic<bool> m_is_active = true;
        bool m_hierarchy_visibility   = true;
        std::array<std::shared_ptr<Component>, 13> m_components;

        void UpdateTransform();
        Math::Matrix GetParentTransformMatrix() const;

        // local
        Math::Vector3 m_position_local    = Math::Vector3::Zero;
        Math::Quaternion m_rotation_local = Math::Quaternion::Identity;
        Math::Vector3 m_scale_local       = Math::Vector3::One;

        Math::Matrix m_matrix          = Math::Matrix::Identity;
        Math::Matrix m_matrix_previous = Math::Matrix::Identity;
        Math::Matrix m_matrix_local    = Math::Matrix::Identity;

        std::weak_ptr<Entity> m_parent;  // the parent of this entity
        std::vector<Entity*> m_children; // the children of this entity

        // misc
        std::mutex m_mutex_children;
        std::mutex m_mutex_parent;
        uint64_t m_transform_changed_frame = 0;
    };
}
