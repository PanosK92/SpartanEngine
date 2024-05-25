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

//= INCLUDES ========================
#include "pch.h"
#include "Entity.h"
#include "Components/Camera.h"
#include "Components/Constraint.h"
#include "Components/Light.h"
#include "Components/PhysicsBody.h"
#include "Components/AudioSource.h"
#include "Components/AudioListener.h"
#include "Components/Terrain.h"
#include "../IO/FileStream.h"
#include "../Rendering/Renderer.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        // input is an entity, output is a clone of that entity (descendant entities are not cloned)
        shared_ptr<Entity> clone_entity(Entity* entity)
        {
            // clone basic properties
            shared_ptr<Entity> clone = World::CreateEntity();
            clone->SetObjectId(SpartanObject::GenerateObjectId());
            clone->SetObjectName(entity->GetObjectName());
            clone->SetActive(entity->IsActive());
            clone->SetHierarchyVisibility(entity->IsVisibleInHierarchy());
            clone->SetPosition(entity->GetPositionLocal());
            clone->SetRotation(entity->GetRotationLocal());
            clone->SetScale(entity->GetScaleLocal());

            // clone all the components
            for (shared_ptr<Component> component_original : entity->GetAllComponents())
            {
                if (component_original != nullptr)
                {
                    // component
                    shared_ptr<Component> component_clone = clone->AddComponent(component_original->GetType());

                    // component's properties
                    component_clone->SetAttributes(component_original->GetAttributes());
                }
            }

            return clone;
        };

        // input is an entity, output is a clone of that entity (descendant entities are cloned)
        shared_ptr<Entity> clone_entity_and_descendants(Entity* entity)
        {
            shared_ptr<Entity> clone_self = clone_entity(entity);

            // clone children make them call this lambda
            for (Entity* child_transform : entity->GetChildren())
            {
                shared_ptr<Entity> clone_child = clone_entity_and_descendants(child_transform);
                clone_child->SetParent(clone_self);
            }

            return clone_self;
        };
    }

    Entity::Entity()
    {
        m_object_name          = "Entity";
        m_is_active            = true;
        m_hierarchy_visibility = true;

        m_components.fill(nullptr);
    }

    Entity::~Entity()
    {
        m_components.fill(nullptr);
    }

    void Entity::Initialize()
    {
        UpdateTransform();
    }

    shared_ptr<Entity> Entity::Clone()
    {
        return clone_entity_and_descendants(this);
    }

    void Entity::OnStart()
    {
        for (shared_ptr<Component> component : m_components)
        {
            if (component)
            {
                component->OnStart();
            }
        }
    }

    void Entity::OnStop()
    {
        for (shared_ptr<Component> component : m_components)
        {
            if (component)
            {
                component->OnStop();
            }
        }
    }

    void Entity::Tick()
    {
        if (!m_is_active)
            return;

        for (shared_ptr<Component>& component : m_components)
        {
            if (component)
            {
                component->OnTick();
            }
        }
    }

    void Entity::Serialize(FileStream* stream)
    {
        // BASIC DATA
        {
            stream->Write(m_is_active);
            stream->Write(m_hierarchy_visibility);
            stream->Write(m_object_id);
            stream->Write(m_object_name);
            stream->Write(m_position_local);
            stream->Write(m_rotation_local);
            stream->Write(m_scale_local);
            stream->Write(!m_parent.expired() ? m_parent.lock()->GetObjectId() : 0);
        }

        // COMPONENTS
        {
            for (shared_ptr<Component>& component : m_components)
            {
                if (component)
                {
                    stream->Write(static_cast<uint32_t>(component->GetType()));
                    stream->Write(component->GetObjectId());
                }
                else
                {
                    stream->Write(static_cast<uint32_t>(ComponentType::Max));
                }
            }

            for (shared_ptr<Component>& component : m_components)
            {
                if (component)
                {
                    component->Serialize(stream);
                }
            }
        }

        // CHILDREN
        {
            vector<Entity*>& children = GetChildren();

            // children count
            stream->Write(static_cast<uint32_t>(children.size()));

            // children IDs
            for (Entity* child : children)
            {
                stream->Write(child->GetObjectId());
            }

            // children
            for (Entity* child : children)
            {
                if (child)
                {
                    child->Serialize(stream);
                }
            }
        }
    }

    void Entity::Deserialize(FileStream* stream, shared_ptr<Entity> parent)
    {
        // BASIC DATA
        {
            stream->Read(&m_is_active);
            stream->Read(&m_hierarchy_visibility);
            stream->Read(&m_object_id);
            stream->Read(&m_object_name);
            stream->Read(&m_position_local);
            stream->Read(&m_rotation_local);
            stream->Read(&m_scale_local);

            uint64_t parent_entity_id = 0;
            stream->Read(&parent_entity_id);
            if (parent_entity_id != 0)
            {
                if (const shared_ptr<Entity>& parent = World::GetEntityById(parent_entity_id))
                {
                    parent->AddChild(this);
                }
            }

            UpdateTransform();
        }

        // COMPONENTS
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(m_components.size()); i++)
            {
                // type
                uint32_t component_type = static_cast<uint32_t>(ComponentType::Max);
                stream->Read(&component_type);

                if (component_type != static_cast<uint32_t>(ComponentType::Max))
                {
                    // id
                    uint64_t component_id = 0;
                    stream->Read(&component_id);

                    shared_ptr<Component> component = AddComponent(static_cast<ComponentType>(component_type));
                    component->SetObjectId(component_id);
                }
            }

            // Sometimes there are component dependencies, e.g. a collider that needs
            // to set it's shape to a rigibody. So, it's important to first create all 
            // the components (like above) and then deserialize them (like here).
            for (shared_ptr<Component>& component : m_components)
            {
                if (component)
                {
                    component->Deserialize(stream);
                }
            }

            // set the transform's parent
            SetParent(parent);
        }

        // CHILDREN
        {
            // Children count
            const uint32_t children_count = stream->ReadAs<uint32_t>();

            // Children IDs
            vector<weak_ptr<Entity>> children;
            for (uint32_t i = 0; i < children_count; i++)
            {
                shared_ptr<Entity> child = World::CreateEntity();

                child->SetObjectId(stream->ReadAs<uint64_t>());

                children.emplace_back(child);
            }

            // Children
            for (const auto& child : children)
            {
                child.lock()->Deserialize(stream, World::GetEntityById(m_object_id));
            }

            AcquireChildren();
        }

        World::Resolve();
    }

    bool Entity::IsActive() const
    {
        if (shared_ptr<Entity> parent = GetParent())
        {
            return m_is_active && parent->IsActive();
        }

        return m_is_active;
    }
    
    shared_ptr<Component> Entity::AddComponent(const ComponentType type)
    {
        shared_ptr<Component> component = nullptr;

        switch (type)
        {
            case ComponentType::AudioListener: component = static_pointer_cast<Component>(AddComponent<AudioListener>()); break;
            case ComponentType::AudioSource:   component = static_pointer_cast<Component>(AddComponent<AudioSource>());   break;
            case ComponentType::Camera:        component = static_pointer_cast<Component>(AddComponent<Camera>());        break;
            case ComponentType::Constraint:    component = static_pointer_cast<Component>(AddComponent<Constraint>());    break;
            case ComponentType::Light:         component = static_pointer_cast<Component>(AddComponent<Light>());         break;
            case ComponentType::Renderable:    component = static_pointer_cast<Component>(AddComponent<Renderable>());    break;
            case ComponentType::PhysicsBody:   component = static_pointer_cast<Component>(AddComponent<PhysicsBody>());   break;
            case ComponentType::Terrain:       component = static_pointer_cast<Component>(AddComponent<Terrain>());       break;
            default:                           component = nullptr;                                                       break;
        }

        SP_ASSERT(component != nullptr);

        return component;
    }

    void Entity::RemoveComponentById(const uint64_t id)
    {
        for (shared_ptr<Component>& component : m_components)
        {
            if (component)
            {
                if (id == component->GetObjectId())
                {
                    component->OnRemove();
                    component = nullptr;
                    break;
                }
            }
        }

        World::Resolve();
    }

    void Entity::UpdateTransform()
    {
        // compute local transform
        m_matrix_local = Matrix(m_position_local, m_rotation_local, m_scale_local);

        // compute world transform
        if (!m_parent.expired())
        {
            m_matrix = m_matrix_local * m_parent.lock()->GetMatrix();
        }
        else
        {
            m_matrix = m_matrix_local;
        }

        // update children
        for (Entity* child : m_children)
        {
            child->UpdateTransform();
        }

        m_transform_changed_frame = Renderer::GetFrameNum();
    }

    void Entity::SetPosition(const Vector3& position)
    {
        if (GetPosition() == position)
            return;

        SetPositionLocal(!HasParent() ? position : position * GetParent()->GetMatrix().Inverted());
    }

    void Entity::SetPositionLocal(const Vector3& position)
    {
        if (m_position_local == position)
            return;

        m_position_local = position;
        UpdateTransform();
    }

    void Entity::SetRotation(const Quaternion& rotation)
    {
        if (GetRotation() == rotation)
            return;

        SetRotationLocal(!HasParent() ? rotation : rotation * GetParent()->GetRotation().Inverse());
    }

    void Entity::SetRotationLocal(const Quaternion& rotation)
    {
        if (m_rotation_local == rotation)
            return;

        m_rotation_local = rotation;
        UpdateTransform();
    }

    void Entity::SetScale(const Vector3& scale)
    {
        if (GetScale() == scale)
            return;

        SetScaleLocal(!HasParent() ? scale : scale / GetParent()->GetScale());
    }

    void Entity::SetScaleLocal(const Vector3& scale)
    {
        if (m_scale_local == scale)
            return;

        m_scale_local = scale;

        // a scale of 0 will cause a division by zero when decomposing the world transform matrix
        m_scale_local.x = (m_scale_local.x == 0.0f) ? Helper::SMALL_FLOAT : m_scale_local.x;
        m_scale_local.y = (m_scale_local.y == 0.0f) ? Helper::SMALL_FLOAT : m_scale_local.y;
        m_scale_local.z = (m_scale_local.z == 0.0f) ? Helper::SMALL_FLOAT : m_scale_local.z;

        UpdateTransform();
    }

    void Entity::Translate(const Vector3& delta)
    {
        if (!HasParent())
        {
            SetPositionLocal(m_position_local + delta);
        }
        else
        {
            SetPositionLocal(m_position_local + GetParent()->GetMatrix().Inverted() * delta);
        }
    }

    void Entity::Rotate(const Quaternion& delta)
    {
        if (!HasParent())
        {
            SetRotationLocal((delta * m_rotation_local).Normalized());
        }
        else
        {
            SetRotationLocal(delta * m_rotation_local * GetRotation().Inverse() * delta * GetRotation());
        }
    }

    Vector3 Entity::GetUp() const
    {
        return GetRotation() * Vector3::Up;
    }

    Vector3 Entity::GetDown() const
    {
        return GetRotation() * Vector3::Down;
    }

    Vector3 Entity::GetForward() const
    {
        return GetRotation() * Vector3::Forward;
    }

    Vector3 Entity::GetBackward() const
    {
        return GetRotation() * Vector3::Backward;
    }

    Vector3 Entity::GetRight() const
    {
        return GetRotation() * Vector3::Right;
    }

    Vector3 Entity::GetLeft() const
    {
        return GetRotation() * Vector3::Left;
    }

    Entity* Entity::GetChildByIndex(const uint32_t index)
    {
        if (!HasChildren() || index >= GetChildrenCount())
            return nullptr;

        return m_children[index];
    }

    Entity* Entity::GetChildByName(const string& name)
    {
        for (Entity* child : m_children)
        {
            if (child->GetObjectName() == name)
                return child;
        }

        return nullptr;
    }

    void Entity::SetParent(weak_ptr<Entity> new_parent_in)
    {
        lock_guard lock(m_mutex_parent);

        shared_ptr<Entity> new_parent = new_parent_in.lock();
        shared_ptr<Entity> parent     = m_parent.lock();

        if (new_parent)
        {
            // early exit if the parent is this entity
            if (GetObjectId() == new_parent->GetObjectId())
                return;
        
            // early exit if the parent is already set
            if (parent && parent->GetObjectId() == new_parent->GetObjectId())
                return;
        
            // if the new parent is a descendant of this transform (e.g. dragging and dropping an entity onto one of it's children)
            if (new_parent->IsDescendantOf(this))
            {
                for (Entity* child : m_children)
                {
                    child->m_parent = m_parent; // directly setting parent
                    child->UpdateTransform();   // update transform if needed
                }
        
                m_children.clear();
            }
        }
        
        // remove the this as a child from the existing parent
        if (parent)
        {
            bool update_child_with_null_parent = false;
            parent->RemoveChild(this, update_child_with_null_parent);
        }
        
        // add this is a child to new parent
        if (new_parent)
        {
            new_parent->AddChild(this);
        }

        if ((parent && !new_parent) || (!parent && new_parent))
        {
            UpdateTransform();
        }

        m_parent = new_parent_in;
    }

    void Entity::AddChild(Entity* child)
    {
        SP_ASSERT(child != nullptr);
        lock_guard lock(m_mutex_children);

        // ensure that the child is not this transform
        if (child->GetObjectId() == GetObjectId())
            return;

        // if this is not already a child, add it
        if (!(find(m_children.begin(), m_children.end(), child) != m_children.end()))
        {
            m_children.emplace_back(child);
        }
    }

    void Entity::RemoveChild(Entity* child, bool update_child_with_null_parent)
    {
        SP_ASSERT(child != nullptr);

        // ensure the transform is not itself
        if (child->GetObjectId() == GetObjectId())
            return;

        lock_guard lock(m_mutex_children);

        // remove the child
        m_children.erase(remove_if(m_children.begin(), m_children.end(), [child](Entity* vec_transform) { return vec_transform->GetObjectId() == child->GetObjectId(); }), m_children.end());

        // remove the child's parent
        if (update_child_with_null_parent)
        {
            shared_ptr<Entity> null = nullptr;
            child->SetParent(null);
        }
    }

    // searches the entire hierarchy, finds any children and saves them in m_children
    // this is a recursive function, the children will also find their own children and so on
    void Entity::AcquireChildren()
    {
        lock_guard lock(m_mutex_children);
        m_children.clear();
        m_children.shrink_to_fit();

        auto entities = World::GetAllEntities();
        for (const auto& possible_child : entities)
        {
            if (!possible_child || !possible_child->HasParent() || possible_child->GetObjectId() == GetObjectId())
                continue;

            // if it's parent matches this transform
            if (possible_child->GetParent()->GetObjectId() == GetObjectId())
            {
                // welcome home son
                m_children.emplace_back(possible_child.get());

                // make the child do the same thing all over, essentially resolving the entire hierarchy
                possible_child->AcquireChildren();
            }
        }
    }

    bool Entity::IsDescendantOf(Entity* transform) const
    {
        SP_ASSERT(transform != nullptr);

        if (m_parent.expired())
            return false;

        if (m_parent.lock()->GetObjectId() == transform->GetObjectId())
            return true;

        for (Entity* child : transform->GetChildren())
        {
            if (IsDescendantOf(child))
                return true;
        }

        return false;
    }

    void Entity::GetDescendants(vector<Entity*>* descendants)
    {
        for (Entity* child : m_children)
        {
            descendants->emplace_back(child);

            if (child->HasChildren())
            {
                child->GetDescendants(descendants);
            }
        }
    }

    Entity* Entity::GetDescendantByName(const string& name)
    {
        vector<Entity*> descendants;
        GetDescendants(&descendants);

        for (Entity* entity : descendants)
        {
            if (entity->GetObjectName() == name)
                return entity;
        }

        return nullptr;
    }

    bool Entity::HasTransformChanged() const
    {
        return m_transform_changed_frame == Renderer::GetFrameNum();
    }

    Matrix Entity::GetParentTransformMatrix() const
    {
        return HasParent() ? GetParent()->GetMatrix() : Matrix::Identity;
    }
}
