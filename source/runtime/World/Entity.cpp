/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ======================
#include "pch.h"
#include "Entity.h"
#include "Prefab.h"
#include "Components/AudioSource.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/Physics.h"
#include "Components/Script.h"
#include "Components/Spline.h"
#include "Components/SplineFollower.h"
#include "Components/Terrain.h"
#include "Components/Volume.h"
#include "Components/ParticleSystem.h"
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//=================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        // input is an entity, output is a clone of that entity (descendant entities are not cloned)
        Entity* clone_entity(Entity* entity)
        {
            // clone basic properties
            Entity* clone = World::CreateEntity();
            clone->SetObjectName(entity->GetObjectName());
            clone->SetActive(entity->GetActive());
            clone->SetPosition(entity->GetPositionLocal());
            clone->SetRotation(entity->GetRotationLocal());
            clone->SetScale(entity->GetScaleLocal());

            // clone all the components
            for (shared_ptr<Component> component_original : entity->GetAllComponents())
            {
                if (component_original != nullptr)
                {
                    // component
                    Component* component_clone = clone->AddComponent(component_original->GetType());

                    // component's properties
                    component_clone->SetAttributes(component_original->GetAttributes());
                }
            }

            return clone;
        };

        // input is an entity, output is a clone of that entity (descendant entities are cloned)
        Entity* clone_entity_and_descendants(Entity* entity)
        {
            Entity* clone_self = clone_entity(entity);

            // clone children make them call this lambda
            for (Entity* child_transform : entity->GetChildren())
            {
                Entity* clone_child = clone_entity_and_descendants(child_transform);
                clone_child->SetParent(clone_self);
            }

            return clone_self;
        };

    }

    Entity::Entity()
    {
        m_object_name = "Entity";
        m_is_active   = true;

        m_components.fill(nullptr);
    }

    Entity::~Entity()
    {
        m_components.fill(nullptr);

        // if this entity is selected, deselect it
        if (Camera* camera = World::GetCamera())
        {
            if (Entity* selected_entity = camera->GetSelectedEntity())
            {
                if (selected_entity->GetObjectId() == GetObjectId())
                {
                    camera->SetSelectedEntity(nullptr);
                }
            }
        }
    }

    Entity* Entity::Clone()
    {
        return clone_entity_and_descendants(this);
    }

    void Entity::RegisterForScripting(sol::state_view State)
    {
        State.new_usertype<Entity>("Entity",
            "GetComponent", [](Entity* Self, ComponentType Type) -> sol::reference
            {
                if (Component* comp = Self->GetComponentByType(Type))
                {
                    return comp->AsLua(World::GetLuaState());
                }

                return sol::nil;
            },
            "AddComponent", [](Entity* Self, ComponentType Type) -> sol::reference
            {
                if (Component* comp = Self->AddComponentByType(Type))
                {
                    return comp->AsLua(World::GetLuaState());
                }

                return sol::nil;
            },
            "RemoveComponent", [](Entity* Self, ComponentType Type)
            {
                Self->RemoveComponentByType(Type);
            },
            "ForEachChild",    [](Entity* Self, const sol::function& Callback)
            {
                for (Entity* Child : Self->m_children)
                {
                    Callback(Child);
                }
            },

            "GetAllComponents",         &Entity::GetAllComponents,
            "GetComponentCount",        &Entity::GetComponentCount,
            "GetName",                  &Entity::GetObjectName,
            "GetObjectSize",            &Entity::GetObjectSize,
            "GetObjectID",              &Entity::GetObjectId,

            "IsActive",                 &Entity::IsActive,
            "GetChildren",              &Entity::GetChildren,
            "HasChildren",              &Entity::HasChildren,
            "GetParent",                &Entity::GetParent,
            "GetChildByName",           &Entity::GetChildByName,
            "GetChildByIndex",          &Entity::GetChildByIndex,
            "GetChildrenCount",         &Entity::GetChildrenCount,
            "IsDescendantOf",           &Entity::IsDescendantOf,

            "Translate",                &Entity::Translate,
            "Rotate",                   &Entity::Rotate,

            "IsTransient",              &Entity::IsTransient,

            "GetUp",                    &Entity::GetUp,
            "GetDown",                  &Entity::GetDown,
            "GetForward",               &Entity::GetForward,
            "GetBackward",              &Entity::GetBackward,
            "GetRight",                 &Entity::GetRight,
            "GetLeft",                  &Entity::GetLeft,

            "GetPosition",              &Entity::GetPosition,
            "GetPositionLocal",         &Entity::GetPositionLocal,
            "SetPosition",              &Entity::SetPosition,
            "SetPositionLocal",         &Entity::SetPositionLocal,

            "GetRotation",              &Entity::GetRotation,
            "GetRotationLocal",         &Entity::GetRotationLocal,
            "SetRotation",              &Entity::SetRotation,
            "SetRotationLocal",         &Entity::SetRotationLocal,

            "GetScale",                 &Entity::GetScale,
            "GetScaleLocal",            &Entity::GetScaleLocal,
            "SetScale",                 &Entity::SetScale,
            "SetScaleLocal",            &Entity::SetScaleLocal
            );
    }

    void Entity::Start()
    {
        for (shared_ptr<Component> component : m_components)
        {
            if (component)
            {
                component->Start();
            }
        }
    }

    void Entity::Stop()
    {
        for (shared_ptr<Component> component : m_components)
        {
            if (component)
            {
                component->Stop();
            }
        }
    }

    void Entity::PreTick()
    {
        for (shared_ptr<Component>& component : m_components)
        {
            if (component)
            {
                component->PreTick();
            }
        }
    }

    void Entity::Tick()
    {
        for (shared_ptr<Component>& component : m_components)
        {
            if (component)
            {
                component->Tick();
            }
        }

        m_time_since_last_transform_sec += static_cast<float>(Timer::GetDeltaTimeSec());
    }

    void Entity::Save(pugi::xml_node& node)
    {
        // self
        {
            node.append_attribute("name")   = m_object_name.c_str();
            node.append_attribute("id")     = m_object_id;
            node.append_attribute("active") = m_is_active;

            {
                stringstream ss;
                ss << m_position_local.x << " " << m_position_local.y << " " << m_position_local.z;
                node.append_attribute("position") = ss.str().c_str();
            }

            {
                stringstream ss;
                ss << m_rotation_local.x << " " << m_rotation_local.y << " " << m_rotation_local.z << " " << m_rotation_local.w;
                node.append_attribute("rotation") = ss.str().c_str();
            }

            {
                stringstream ss;
                ss << m_scale_local.x << " " << m_scale_local.y << " " << m_scale_local.z;
                node.append_attribute("scale") = ss.str().c_str();
            }

            // if this entity has prefab data, save the prefab reference instead of components/children
            if (HasPrefabData())
            {
                pugi::xml_node prefab_node = node.append_child("prefab");
                prefab_node.append_attribute("type") = m_prefab_type.c_str();
                for (const auto& [key, value] : m_prefab_attributes)
                {
                    prefab_node.append_attribute(key.c_str()) = value.c_str();
                }
                return; // don't save components or children - prefab will recreate them
            }

            // components
            for (shared_ptr<Component>& component : m_components)
            {
                if (component)
                {
                    string type_name              = Component::TypeToString(component->GetType());
                    pugi::xml_node component_node = node.append_child(type_name.c_str());
                    component->Save(component_node);
                }
            }
        }

        // children (skip transient entities - they are dynamically created and shouldn't be serialized)
        for (Entity* child : m_children)
        {
            if (child->IsTransient())
                continue;

            pugi::xml_node child_node = node.append_child("Entity");
            child->Save(child_node);
        }
    }

    void Entity::SetPrefabData(const string& type, const unordered_map<string, string>& attributes)
    {
        m_prefab_type       = type;
        m_prefab_attributes = attributes;
    }

    void Entity::Load(pugi::xml_node& node)
    {
        // self
        {
            m_is_active   = node.attribute("active").as_bool();
            m_object_id   = node.attribute("id").as_ullong();
            m_object_name = node.attribute("name").as_string();

            {
                string pos_str = node.attribute("position").as_string();
                stringstream ss(pos_str);
                ss >> m_position_local.x >> m_position_local.y >> m_position_local.z;
            }

            {
                string rot_str = node.attribute("rotation").as_string();
                stringstream ss(rot_str);
                ss >> m_rotation_local.x >> m_rotation_local.y >> m_rotation_local.z >> m_rotation_local.w;
            }

            {
                string scale_str = node.attribute("scale").as_string();
                stringstream ss(scale_str);
                ss >> m_scale_local.x >> m_scale_local.y >> m_scale_local.z;
            }

            // components and prefabs
            for (pugi::xml_node component_node = node.first_child(); component_node; component_node = component_node.next_sibling())
            {
                string type_name = component_node.name();
                if (type_name == "Entity")
                    continue; // skip children, handled below

                // check for prefab node - creates complex entity hierarchies
                if (type_name == "prefab")
                {
                    // store prefab data for saving later
                    string prefab_type = component_node.attribute("type").as_string();
                    unordered_map<string, string> prefab_attributes;
                    for (pugi::xml_attribute attr = component_node.first_attribute(); attr; attr = attr.next_attribute())
                    {
                        prefab_attributes[attr.name()] = attr.value();
                    }
                    SetPrefabData(prefab_type, prefab_attributes);

                    // create the prefab
                    Prefab::Create(component_node, this);
                    continue;
                }

                ComponentType type = Component::StringToType(type_name);
                if (type != ComponentType::Max)
                {
                    if (Component* component = AddComponent(type))
                    {
                        component->Load(component_node);
                    }
                }
            }
        }

        // children
        for (pugi::xml_node child_node = node.child("Entity"); child_node; child_node = child_node.next_sibling("Entity"))
        {
            Entity* child = World::CreateEntity();
            child->Load(child_node);
            child->SetParent(this);
        }

        UpdateTransform();
    }

    bool Entity::GetActive()
    {
        if (Entity* parent = GetParent())
        {
            return m_is_active && parent->GetActive();
        }

        return m_is_active;
    }

    void Entity::SetActive(const bool active)
    {
        if (active == m_is_active)
            return;

        m_is_active = active;
    }

    Component* Entity::GetComponentByType(ComponentType Type) const
    {
        return m_components[static_cast<uint32_t>(Type)].get();
    }

    Component* Entity::AddComponentByType(ComponentType Type)
    {
        if (Component* component = GetComponentByType(Type))
        {
            return component;
        }

        std::shared_ptr<Component> component;
        switch (Type)
        {
        case ComponentType::AudioSource:
            component = std::make_shared<AudioSource>(this);
            break;
        case ComponentType::Camera:
            component = std::make_shared<Camera>(this);
            break;
        case ComponentType::Light:
            component = std::make_shared<Light>(this);
            break;
        case ComponentType::Physics:
            component = std::make_shared<Physics>(this);
            break;
        case ComponentType::Renderable:
            component = std::make_shared<Renderable>(this);
            break;
        case ComponentType::Spline:
            component = std::make_shared<Spline>(this);
            break;
        case ComponentType::SplineFollower:
            component = std::make_shared<SplineFollower>(this);
            break;
        case ComponentType::Terrain:
            component = std::make_shared<Terrain>(this);
            break;
        case ComponentType::Volume:
            component = std::make_shared<Volume>(this);
            break;
        case ComponentType::Script:
            component = std::make_shared<Script>(this);
            break;
        case ComponentType::ParticleSystem:
            component = std::make_shared<ParticleSystem>(this);
            break;
        case ComponentType::Max:
            break;
        }

        m_components[static_cast<uint32_t>(Type)] = component;

        component->SetType(Type);
        component->Initialize();

        return component.get();
    }

    void Entity::RemoveComponentByType(ComponentType Type)
    {
        m_components[static_cast<uint32_t>(Type)] = nullptr;
    }

    Component* Entity::AddComponent(const ComponentType type)
    {
        Component* component = nullptr;

        switch (type)
        {
            // auto-generated from SP_COMPONENT_LIST
            #define X(type, str) case ComponentType::type: component = static_cast<Component*>(AddComponent<type>()); break;
            SP_COMPONENT_LIST
            #undef X
            default: component = nullptr; break;
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
                    component->Remove();
                    component = nullptr;
                    break;
                }
            }
        }
    }

    uint32_t Entity::GetComponentCount() const
    {
        uint32_t count = 0;
        for (const shared_ptr<Component>& component : m_components)
        {
            if (component)
            {
                count++;
            }
        }

        return count;
    }

    void Entity::UpdateTransform()
    {
        // compute local transform
        m_matrix_local = Matrix(m_position_local, m_rotation_local, m_scale_local);

        // compute world transform
        if (m_parent)
        {
            m_matrix = m_matrix_local * m_parent->GetMatrix();
        }
        else
        {
            m_matrix = m_matrix_local;
        }

        // update directions directly from matrix (avoids unstable quaternion decomposition)
        // row-major layout: row 0 = right (X), row 1 = up (Y), row 2 = forward (Z)
        {
            // x
            m_right    = Vector3::Normalize(Vector3(m_matrix.m00, m_matrix.m01, m_matrix.m02));
            m_left     = -m_right;
            // y
            m_up       = Vector3::Normalize(Vector3(m_matrix.m10, m_matrix.m11, m_matrix.m12));
            m_down     = -m_up;
            // z
            m_forward  = Vector3::Normalize(Vector3(m_matrix.m20, m_matrix.m21, m_matrix.m22));
            m_backward = -m_forward;
        }

        // mark update
        m_time_since_last_transform_sec = 0.0f;

        // update children
        for (Entity* child : m_children)
        {
            child->UpdateTransform();
        }
    }

    void Entity::SetPosition(const Vector3& position)
    {
        if (GetPosition() == position)
            return;

        SetPositionLocal(!GetParent() ? position : position * GetParent()->GetMatrix().Inverted());
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
        // compute local rotation without using unstable GetRotation() decomposition
        Quaternion local_rotation;
        if (!GetParent())
        {
            local_rotation = rotation;
        }
        else
        {
            // compute parent's world rotation by composing local rotations up the hierarchy
            // world_rot = root_local * ... * parent_local (compose from root down)
            vector<Quaternion> rotations;
            Entity* ancestor = GetParent();
            while (ancestor)
            {
                rotations.push_back(ancestor->GetRotationLocal());
                ancestor = ancestor->GetParent();
            }

            // compose from root (back of vector) to parent (front of vector)
            Quaternion parent_world_rotation = Quaternion::Identity;
            for (auto it = rotations.rbegin(); it != rotations.rend(); ++it)
            {
                parent_world_rotation = parent_world_rotation * (*it);
            }

            local_rotation = parent_world_rotation.Inverse() * rotation;
        }

        SetRotationLocal(local_rotation);
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

        SetScaleLocal(!GetParent() ? scale : scale / GetParent()->GetScale());
    }

    void Entity::SetScaleLocal(const Vector3& scale)
    {
        if (m_scale_local == scale)
            return;

        m_scale_local = scale;

        // a scale of 0 will cause a division by zero when decomposing the world transform matrix
        m_scale_local.x = (m_scale_local.x == 0.0f) ? numeric_limits<float>::min() : m_scale_local.x;
        m_scale_local.y = (m_scale_local.y == 0.0f) ? numeric_limits<float>::min() : m_scale_local.y;
        m_scale_local.z = (m_scale_local.z == 0.0f) ? numeric_limits<float>::min() : m_scale_local.z;

        UpdateTransform();
    }

    void Entity::Translate(const Vector3& delta)
    {
        if (!GetParent())
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
        if (!GetParent())
        {
            SetRotationLocal((delta * m_rotation_local).Normalized());
        }
        else
        {
            SetRotationLocal(GetParent()->GetRotation().Inverse() * delta * GetParent()->GetRotation() * m_rotation_local);
        }
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

    void Entity::SetParent(Entity* new_parent)
    {
        lock_guard lock(m_mutex_parent);

        if (new_parent)
        {
            // early exit if the parent is this entity
            if (GetObjectId() == new_parent->GetObjectId())
                return;

            // early exit if the parent is already set
            if (m_parent && m_parent->GetObjectId() == new_parent->GetObjectId())
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
        if (m_parent)
        {
            bool update_child_with_null_parent = false;
            m_parent->RemoveChild(this, update_child_with_null_parent);
        }

        // add this is a child to new parent
        if (new_parent)
        {
            new_parent->AddChild(this);
        }

        m_parent = new_parent;
        UpdateTransform();
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

    void Entity::MoveChildToIndex(Entity* child, uint32_t index)
    {
        SP_ASSERT(child != nullptr);
        lock_guard lock(m_mutex_children);

        // find the child in the list
        auto it = find(m_children.begin(), m_children.end(), child);
        if (it == m_children.end())
            return; // child not found

        // get current position before removing
        uint32_t current_index = static_cast<uint32_t>(distance(m_children.begin(), it));

        // remove from current position
        m_children.erase(it);

        // adjust target index if the child was before the target position
        // (removing it shifts all subsequent indices down by 1)
        if (current_index < index && index > 0)
            index--;

        // clamp index to valid range
        if (index > m_children.size())
            index = static_cast<uint32_t>(m_children.size());

        // insert at new position
        m_children.insert(m_children.begin() + index, child);
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
            child->SetParent(nullptr);
        }
    }

    // searches the entire hierarchy, finds any children and saves them in m_children
    // this is a recursive function, the children will also find their own children and so on
    void Entity::AcquireChildren()
    {
        lock_guard lock(m_mutex_children);
        m_children.clear();
        m_children.shrink_to_fit();

        const vector<Entity*>& entities = World::GetEntities();
        for (Entity* possible_child : entities)
        {
            if (!possible_child || !possible_child->GetParent() || possible_child->GetObjectId() == GetObjectId())
                continue;

            // if it's parent matches this transform
            if (possible_child->GetParent()->GetObjectId() == GetObjectId())
            {
                // welcome home son
                m_children.emplace_back(possible_child);

                // make the child do the same thing all over, essentially resolving the entire hierarchy
                possible_child->AcquireChildren();
            }
        }
    }

    bool Entity::IsDescendantOf(Entity* transform) const
    {
        SP_ASSERT(transform != nullptr);

        if (!m_parent)
            return false;

        if (m_parent->GetObjectId() == transform->GetObjectId())
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

    Matrix Entity::GetParentTransformMatrix()
    {
        return GetParent() ? GetParent()->GetMatrix() : Matrix::Identity;
    }
}
