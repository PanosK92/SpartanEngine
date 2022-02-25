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

//= INCLUDES ==========================
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
#include "Components/ReflectionProbe.h"
#include "../IO/FileStream.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    Entity::Entity(Context* context, uint64_t transform_id /*= 0*/)
    {
        m_context               = context;
        m_object_name           = "Entity";
        m_is_active             = true;
        m_hierarchy_visibility  = true;
        AddComponent<Transform>(transform_id);
    }

    Entity::~Entity()
    {
        for (auto it = m_components.begin(); it != m_components.end();)
        {
            (*it)->OnRemove();
            (*it).reset();
            it = m_components.erase(it);
        }
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
            clone->SetObjectId(GenerateObjectId());
            clone->SetName(entity->GetObjectName());
            clone->SetActive(entity->IsActive());
            clone->SetHierarchyVisibility(entity->IsVisibleInHierarchy());

            // Clone all the components
            for (const auto& component : entity->GetAllComponents())
            {
                const auto& original_comp = component;
                auto clone_comp           = clone->AddComponent(component->GetType());
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

	void Entity::Tick(double delta_time)
    {
        if (!m_is_active)
            return;

        // call component Update()
        for (shared_ptr<IComponent>& component : m_components)
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
            stream->Write(GetObjectId());
            stream->Write(m_object_name);
        }

        // COMPONENTS
        {
            stream->Write(static_cast<uint32_t>(m_components.size()));

            for (shared_ptr<IComponent>& component : m_components)
            {
                stream->Write(static_cast<uint32_t>(component->GetType()));
                stream->Write(component->GetObjectId());
            }

            for (shared_ptr<IComponent>& component : m_components)
            {
                component->Serialize(stream);
            }
        }

        // CHILDREN
        {
            vector<Transform*>& children = GetTransform()->GetChildren();

            // Children count
            stream->Write(static_cast<uint32_t>(children.size()));

            // Children IDs
            for (Transform* child : children)
            {
                stream->Write(child->GetObjectId());
            }

            // Children
            for (Transform* child : children)
            {
                if (child->GetEntity())
                {
                    child->GetEntity()->Serialize(stream);
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
            stream->Read(&m_object_id);
            stream->Read(&m_object_name);
        }

        // COMPONENTS
        {
            const uint32_t component_count = stream->ReadAs<uint32_t>();

            for (uint32_t i = 0; i < component_count; i++)
            {
                uint32_t component_type = static_cast<uint32_t>(ComponentType::Unknown);
                uint64_t component_id   = 0;

                stream->Read(&component_type); // load component's type
                stream->Read(&component_id);   // load component's id

                AddComponent(static_cast<ComponentType>(component_type), component_id);
            }

            // Sometimes there are component dependencies, e.g. a collider that needs
            // to set it's shape to a rigibody. So, it's important to first create all 
            // the components (like above) and then deserialize them (like here).
            for (shared_ptr<IComponent>& component : m_components)
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
            const uint32_t children_count = stream->ReadAs<uint32_t>();

            // Children IDs
            World* world = m_context->GetSubsystem<World>();
            vector<weak_ptr<Entity>> children;
            for (uint32_t i = 0; i < children_count; i++)
            {
                shared_ptr<Entity> child = world->EntityCreate();

                child->SetObjectId(stream->ReadAs<uint64_t>());

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
        SP_FIRE_EVENT(EventType::WorldResolve);
    }

    IComponent* Entity::AddComponent(const ComponentType type, uint64_t id /*= 0*/)
    {
        // This is the only hardcoded part regarding components. It's 
        // one function but it would be nice if that gets automated too.

        IComponent* component = nullptr;

        switch (type)
        {
            case ComponentType::AudioListener:   component = static_cast<IComponent*>(AddComponent<AudioListener>(id));   break;
            case ComponentType::AudioSource:     component = static_cast<IComponent*>(AddComponent<AudioSource>(id));     break;
            case ComponentType::Camera:          component = static_cast<IComponent*>(AddComponent<Camera>(id));          break;
            case ComponentType::Collider:        component = static_cast<IComponent*>(AddComponent<Collider>(id));        break;
            case ComponentType::Constraint:      component = static_cast<IComponent*>(AddComponent<Constraint>(id));      break;
            case ComponentType::Light:           component = static_cast<IComponent*>(AddComponent<Light>(id));           break;
            case ComponentType::Renderable:      component = static_cast<IComponent*>(AddComponent<Renderable>(id));      break;
            case ComponentType::RigidBody:       component = static_cast<IComponent*>(AddComponent<RigidBody>(id));       break;
            case ComponentType::SoftBody:        component = static_cast<IComponent*>(AddComponent<SoftBody>(id));        break;
            case ComponentType::Script:          component = static_cast<IComponent*>(AddComponent<Script>(id));          break;
            case ComponentType::Environment:     component = static_cast<IComponent*>(AddComponent<Environment>(id));     break;
            case ComponentType::Transform:       component = static_cast<IComponent*>(AddComponent<Transform>(id));       break;
            case ComponentType::Terrain:         component = static_cast<IComponent*>(AddComponent<Terrain>(id));         break;
            case ComponentType::ReflectionProbe: component = static_cast<IComponent*>(AddComponent<ReflectionProbe>(id)); break;
            case ComponentType::Unknown:         component = nullptr;                                                     break;
            default:                             component = nullptr;                                                     break;
        }

        SP_ASSERT(component != nullptr);

        return component;
    }

    void Entity::RemoveComponentById(const uint64_t id)
    {
        ComponentType component_type = ComponentType::Unknown;

        for (auto it = m_components.begin(); it != m_components.end(); ) 
        {
            auto component = *it;
            if (id == component->GetObjectId())
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
        SP_FIRE_EVENT(EventType::WorldResolve);
    }
}
