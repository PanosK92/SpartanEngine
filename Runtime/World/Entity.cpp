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

//= INCLUDES ========================
#include "Spartan.h"
#include "Entity.h"
#include "World.h"
#include "Components/Camera.h"
#include "Components/Collider.h"
#include "Components/Transform.h"
#include "Components/Constraint.h"
#include "Components/Light.h"
#include "Components/Renderable.h"
#include "Components/RigidBody.h"
#include "Components/SoftBody.h"
#include "Components/Environment.h"
#include "Components/Script.h"
#include "Components/AudioSource.h"
#include "Components/AudioListener.h"
#include "Components/Terrain.h"
#include "../IO/FileStream.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    Entity::Entity(Context* context, uint32_t transform_id /*= 0*/)
    {
        m_context               = context;
        m_name                  = "Entity";
        m_is_active             = true;
        m_hierarchy_visibility  = true;
        AddComponent<Transform>(transform_id);
    }

    Entity::~Entity()
    {
        m_is_active             = false;
        m_hierarchy_visibility  = false;
        m_transform             = nullptr;
        m_renderable            = nullptr;
        m_context               = nullptr;
        m_name.clear();
        m_component_mask = 0;
        for (auto it = m_components.begin(); it != m_components.end();)
        {
            (*it)->OnRemove();
            (*it).reset();
            it = m_components.erase(it);
        }
        m_components.clear();
    }

    void Entity::Clone()
    {
        auto scene = m_context->GetSubsystem<World>();
        vector<Entity*> clones;

        // Creation of new entity and copying of a few properties
        auto clone_entity = [&scene, &clones](Entity* entity)
        {
            // Clone the name and the ID
            auto clone = scene->EntityCreate().get();
            clone->SetId(GenerateId());
            clone->SetName(entity->GetName());
            clone->SetActive(entity->IsActive());
            clone->SetHierarchyVisibility(entity->IsVisibleInHierarchy());

            // Clone all the components
            for (const auto& component : entity->GetAllComponents())
            {
                const auto& original_comp    = component;
                auto clone_comp                = clone->AddComponent(component->GetType());
                clone_comp->SetAttributes(original_comp->GetAttributes());
            }

            clones.emplace_back(clone);

            return clone;
        };

        // Cloning of an entity and it's descendants (this is a recursive lambda)
        function<Entity*(Entity*)> clone_entity_and_descendants = [&clone_entity_and_descendants, &clone_entity](Entity* original)
        {
            // clone self
            const auto clone_self = clone_entity(original);

            // clone children make them call this lambda
            for (const auto& child_transform : original->GetTransform()->GetChildren())
            {
                const auto clone_child = clone_entity_and_descendants(child_transform->GetEntity());
                clone_child->GetTransform()->SetParent(clone_self->GetTransform());
            }

            // return self
            return clone_self;
        };

        // Clone the entire hierarchy
        clone_entity_and_descendants(this);
    }

    void Entity::Start()
    {
        // call component Start()
        for (auto const& component : m_components)
        {
            component->OnStart();
        }
    }

    void Entity::Stop()
    {
        // call component Stop()
        for (auto const& component : m_components)
        {
            component->OnStop();
        }
    }

    void Entity::Tick(float delta_time)
    {
        if (!m_is_active)
            return;

        // call component Update()
        for (const auto& component : m_components)
        {
            component->OnTick(delta_time);
        }
    }

    void Entity::Serialize(FileStream* stream)
    {
        // BASIC DATA
        {
            stream->Write(m_is_active);
            stream->Write(m_hierarchy_visibility);
            stream->Write(GetId());
            stream->Write(m_name);
        }

        // COMPONENTS
        {
            stream->Write(static_cast<uint32_t>(m_components.size()));
            for (const auto& component : m_components)
            {
                stream->Write(static_cast<uint32_t>(component->GetType()));
                stream->Write(component->GetId());
            }

            for (const auto& component : m_components)
            {
                component->Serialize(stream);
            }
        }

        // CHILDREN
        {
            auto children = GetTransform()->GetChildren();

            // Children count
            stream->Write(static_cast<uint32_t>(children.size()));

            // Children IDs
            for (const auto& child : children)
            {
                stream->Write(child->GetId());
            }

            // Children
            for (const auto& child : children)
            {
                if (child->GetEntity())
                {
                    child->GetEntity()->Serialize(stream);
                }
                else
                {
                    LOG_ERROR("Aborting , child entity is nullptr.");
                    break;
                }
            }
        }
    }

    void Entity::Deserialize(FileStream* stream, Transform* parent)
    {
        // BASIC DATA
        {
            stream->Read(&m_is_active);
            stream->Read(&m_hierarchy_visibility);
            stream->Read(&m_id);
            stream->Read(&m_name);
        }

        // COMPONENTS
        {
            const auto component_count = stream->ReadAs<uint32_t>();
            for (uint32_t i = 0; i < component_count; i++)
            {
                uint32_t type   = static_cast<uint32_t>(ComponentType::Unknown);
                uint32_t id     = 0;

                stream->Read(&type);    // load component's type
                stream->Read(&id);        // load component's id

                auto component = AddComponent(static_cast<ComponentType>(type), id);
            }

            // Sometimes there are component dependencies, e.g. a collider that needs
            // to set it's shape to a rigibody. So, it's important to first create all 
            // the components (like above) and then deserialize them (like here).
            for (const auto& component : m_components)
            {
                component->Deserialize(stream);
            }

            // Set the transform's parent
            if (m_transform)
            {
                m_transform->SetParent(parent);
            }
        }

        // CHILDREN
        {
            // Children count
            const auto children_count = stream->ReadAs<uint32_t>();

            // Children IDs
            auto scene = m_context->GetSubsystem<World>();
            vector<std::weak_ptr<Entity>> children;
            for (uint32_t i = 0; i < children_count; i++)
            {
                auto child = scene->EntityCreate();
                child->SetId(stream->ReadAs<uint32_t>());
                children.emplace_back(child);
            }

            // Children
            for (const auto& child : children)
            {
                child.lock()->Deserialize(stream, GetTransform());
            }

            if (m_transform)
            {
                m_transform->AcquireChildren();
            }
        }

        // Make the scene resolve
        FIRE_EVENT(EventType::WorldResolve);
    }

    IComponent* Entity::AddComponent(const ComponentType type, uint32_t id /*= 0*/)
    {
        // This is the only hardcoded part regarding components. It's 
        // one function but it would be nice if that gets automated too, somehow...

        switch (type)
        {
            case ComponentType::AudioListener:    return AddComponent<AudioListener>(id);
            case ComponentType::AudioSource:    return AddComponent<AudioSource>(id);
            case ComponentType::Camera:            return AddComponent<Camera>(id);
            case ComponentType::Collider:        return AddComponent<Collider>(id);
            case ComponentType::Constraint:        return AddComponent<Constraint>(id);
            case ComponentType::Light:            return AddComponent<Light>(id);
            case ComponentType::Renderable:        return AddComponent<Renderable>(id);
            case ComponentType::RigidBody:        return AddComponent<RigidBody>(id);
            case ComponentType::SoftBody:        return AddComponent<SoftBody>(id);
            case ComponentType::Script:            return AddComponent<Script>(id);
            case ComponentType::Environment:    return AddComponent<Environment>(id);
            case ComponentType::Transform:        return AddComponent<Transform>(id);
            case ComponentType::Terrain:           return AddComponent<Terrain>(id);
            case ComponentType::Unknown:        return nullptr;
            default:                            return nullptr;
        }

        return nullptr;
    }

    void Entity::RemoveComponentById(const uint32_t id)
    {
        ComponentType component_type = ComponentType::Unknown;

        for (auto it = m_components.begin(); it != m_components.end(); ) 
        {
            auto component = *it;
            if (id == component->GetId())
            {
                component_type = component->GetType();
                component->OnRemove();
                it = m_components.erase(it);    
                break;
            }
            else
            {
                ++it;
            }
        }

        // The script component can have multiple instance, so only remove
        // it's flag if there are no more components of that type left
        bool others_of_same_type_exist = false;
        for (auto it = m_components.begin(); it != m_components.end(); ++it)
        {
            others_of_same_type_exist = ((*it)->GetType() == component_type) ? true : others_of_same_type_exist;
        }

        if (!others_of_same_type_exist)
        {
            m_component_mask &= ~GetComponentMask(component_type);
        }

        // Make the scene resolve
        FIRE_EVENT(EventType::WorldResolve);
    }
}
