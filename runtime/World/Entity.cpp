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

//= INCLUDES ==========================
#include "pch.h"
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
#include "Components/AudioSource.h"
#include "Components/AudioListener.h"
#include "Components/Terrain.h"
#include "Components/ReflectionProbe.h"
#include "../IO/FileStream.h"
#include "../Rendering/Mesh.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    namespace
    {
        // input is an entity, output is a clone of that entity (descendant entities are not cloned)
        Entity* clone_entity(Entity* entity)
        {
            // clone the name and the ID
            Entity* clone = World::CreateEntity().get();
            clone->SetObjectId(Object::GenerateObjectId());
            clone->SetObjectName(entity->GetObjectName());
            clone->SetActive(entity->IsActive());
            clone->SetHierarchyVisibility(entity->IsVisibleInHierarchy());

            // clone all the components
            for (shared_ptr<Component> component_original : entity->GetAllComponents())
            {
                /*
                 * TODO:
                 * Transfer handling Mesh clone of Renderable component functionality to Renderable class. 
                 * Currently this 'if' block prevents Mesh defaulting to Renderer::GetStandardMesh(Cube).
                 */
                if (component_original !=nullptr)
                {
                    // component
                    shared_ptr<Component> component_clone = clone->AddComponent(component_original->GetType());

                    if (component_original->GetType() == ComponentType::Renderable)
                    {
                        shared_ptr<Renderable> renderable_component = std::dynamic_pointer_cast<Renderable>(component_original);
                        shared_ptr<Renderable> renderable_component_clone = std::dynamic_pointer_cast<Renderable>(component_clone);

                        auto vc          = renderable_component->GetVertexCount();
                        auto vo          = renderable_component->GetVertexOffset();
                        auto gn          = renderable_component->GetGeometryName();
                        auto gt          = renderable_component->GetGeometryType();
                        auto aabb        = renderable_component->GetAabb();
                        auto bb          = renderable_component->GetBoundingBox();
                        auto ic          = renderable_component->GetIndexCount();
                        auto io          = renderable_component->GetIndexOffset();
                        Mesh* mesh_clone = renderable_component->GetMesh();

                        // renderable component's properties
                        renderable_component_clone->SetAttributes(renderable_component->GetAttributes());
                        renderable_component_clone->SetGeometry(gn, io, ic, vo, vc, bb, mesh_clone);
                    }
                    else
                    {
                        SP_LOG_INFO("Cloned component type: %d", component_original->GetType());
                        // component's properties
                        component_clone->SetAttributes(component_original->GetAttributes());
                    }
                }
            }
            return clone;
        };

        // input is an entity, output is a clone of that entity (descendant entities are cloned)
        Entity* clone_entity_and_descendants(Entity* entity)
        {
            Entity* clone_self = clone_entity(entity);

            // clone children make them call this lambda
            for (Transform* child_transform : entity->GetTransform()->GetChildren())
            {
                Entity* clone_child = clone_entity_and_descendants(child_transform->GetEntityPtr());
                clone_child->GetTransform()->SetParent(clone_self->GetTransform());
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
        AddComponent<Transform>();
    }

    void Entity::Clone()
    {
        clone_entity_and_descendants(this);
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

    void Entity::OnPreTick()
    {

    }

	void Entity::Tick()
    {
        if (!m_is_active)
            return;

        for (shared_ptr<Component> component : m_components)
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
            stream->Write(GetObjectId());
            stream->Write(m_object_name);
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
                    stream->Write(static_cast<uint32_t>(ComponentType::Undefined));
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
                if (child->GetEntityPtr())
                {
                    child->GetEntityPtr()->Serialize(stream);
                }
            }
        }
    }

    void Entity::Deserialize(FileStream* stream, shared_ptr<Transform> parent)
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
            for (uint32_t i = 0; i < static_cast<uint32_t>(m_components.size()); i++)
            {
                // Type
                uint32_t component_type = static_cast<uint32_t>(ComponentType::Undefined);
                stream->Read(&component_type);

                if (component_type == static_cast<uint32_t>(ComponentType::Undefined))
                {
                    // Id
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

            // Set the transform's parent
            GetTransform()->SetParent(parent);
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
                child.lock()->Deserialize(stream, GetTransform());
            }

            GetTransform()->AcquireChildren();
        }

        // Make the scene resolve
        SP_FIRE_EVENT(EventType::WorldResolve);
    }

    bool Entity::IsActiveRecursively()
    {
        if (Transform* parent = GetTransform()->GetParent())
        {
            return m_is_active && parent->GetEntityPtr()->IsActiveRecursively();
        }

        return m_is_active;
    }
    
    shared_ptr<Component> Entity::AddComponent(const ComponentType type)
    {
        // This is the only hardcoded part regarding components. It's 
        // one function but it would be nice if that gets automated too.

        shared_ptr<Component> component = nullptr;

        switch (type)
        {
            case ComponentType::AudioListener:   component = static_pointer_cast<Component>(AddComponent<AudioListener>());   break;
            case ComponentType::AudioSource:     component = static_pointer_cast<Component>(AddComponent<AudioSource>());     break;
            case ComponentType::Camera:          component = static_pointer_cast<Component>(AddComponent<Camera>());          break;
            case ComponentType::Collider:        component = static_pointer_cast<Component>(AddComponent<Collider>());        break;
            case ComponentType::Constraint:      component = static_pointer_cast<Component>(AddComponent<Constraint>());      break;
            case ComponentType::Light:           component = static_pointer_cast<Component>(AddComponent<Light>());           break;
            case ComponentType::Renderable:      component = static_pointer_cast<Component>(AddComponent<Renderable>());      break;
            case ComponentType::RigidBody:       component = static_pointer_cast<Component>(AddComponent<RigidBody>());       break;
            case ComponentType::SoftBody:        component = static_pointer_cast<Component>(AddComponent<SoftBody>());        break;
            case ComponentType::Environment:     component = static_pointer_cast<Component>(AddComponent<Environment>());     break;
            case ComponentType::Transform:       component = static_pointer_cast<Component>(AddComponent<Transform>());       break;
            case ComponentType::Terrain:         component = static_pointer_cast<Component>(AddComponent<Terrain>());         break;
            case ComponentType::ReflectionProbe: component = static_pointer_cast<Component>(AddComponent<ReflectionProbe>()); break;
            case ComponentType::Undefined:       component = nullptr;                                                         break;
            default:                             component = nullptr;                                                         break;
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

        // Make the scene resolve
        SP_FIRE_EVENT(EventType::WorldResolve);
    }

    shared_ptr<Transform> Entity::GetTransform()
    {
        return GetComponent<Transform>();
    }
}
