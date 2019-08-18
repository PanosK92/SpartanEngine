/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =================================
#include "Entity.h"
#include "World.h"
#include "../IO/FileStream.h"
#include "../Core/Context.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Collider.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Constraint.h"
#include "../World/Components/Light.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/RigidBody.h"
#include "../World/Components/Skybox.h"
#include "../World/Components/Script.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/AudioListener.h"
//============================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    Entity::Entity(Context* context, uint32_t id /*= 0*/, uint32_t transform_id /*= 0*/) : Spartan_Object(id)
    {
        m_context   = context;
        m_world     = context->GetSubsystem<World>().get();
        m_transform = AddComponent<Transform>(transform_id).get(); // All entities must have a transform
    }

    Entity::~Entity()
	{
		// delete components
        m_world->IterateManagers([&](auto& manager)
        {
            if (auto& component = manager->GetComponent(GetId()))
            {
                component->OnRemove();
                manager->RemoveComponent(GetId());
            }
        });

        // Reset component mask
        m_component_mask = 0;

		m_name.clear();
		m_is_active				= false;
		m_hierarchy_visibility	= false;
	}

	void Entity::Clone()
	{
		vector<Entity*> clones;

		// Creation of new entity and copying of a few properties
		auto clone_entity = [this, &clones](Entity* entity)
		{
			// Clone the name and the ID
            Entity* clone = m_world->EntityCreate().get();
			clone->SetName(entity->GetName());
			clone->SetActive(entity->IsActive());
			clone->SetHierarchyVisibility(entity->IsVisibleInHierarchy());

			// Clone all the components
			for (const auto& component : entity->GetAllComponents())
            {
				auto clone_comp = clone->AddComponent(component->GetType());
				clone_comp->SetAttributes(component->GetAttributes());
			}

			clones.emplace_back(clone);

			return clone;
		};

		// Cloning of an entity and it's descendants (this is a recursive lambda)
		std::function<Entity*(Entity*)> clone_entity_and_descendants = [&clone_entity_and_descendants, &clone_entity](Entity* original)
		{
			// clone self
			const auto clone_self = clone_entity(original);

			// clone children make them call this lambda
			for (const auto& child_transform : original->GetTransform_PtrRaw()->GetChildren())
			{
                Entity* clone_child = clone_entity_and_descendants(child_transform->GetEntity_PtrRaw());
				clone_child->GetTransform_PtrRaw()->SetParent(clone_self->GetTransform_PtrRaw());
			}

			// return self
			return clone_self;
		};

		// Clone the entire hierarchy
		clone_entity_and_descendants(this);
	}

    void Entity::Serialize(FileStream* stream)
	{
		// ENTITY
        {
            stream->Write(m_is_active);
            stream->Write(m_hierarchy_visibility);
            stream->Write(GetId());
            stream->Write(m_name);
        }

		// COMPONENTS
        {
            auto components = GetAllComponents();
            stream->Write(static_cast<uint32_t>(components.size()));
            for (const auto& component : components)
            {
                stream->Write(static_cast<uint32_t>(component->GetType()));
                stream->Write(component->GetId());
            }

            for (const auto& component : components)
            {
                component->Serialize(stream);
            }
        }

		// CHILDREN
        {
            const vector<Transform*>& children = GetTransform_PtrRaw()->GetChildren();

            // 1st - children count
            stream->Write(static_cast<uint32_t>(children.size()));

            // 2nd - children IDs
            for (const auto& child : children)
            {
                stream->Write(child->GetId());
                stream->Write(child->GetTransform()->GetId());
            }

            // 3rd - children
            for (const auto& child : children)
            {
                child->GetEntity_PtrRaw()->Serialize(stream);
            }
        }
	}

	void Entity::Deserialize(FileStream* stream, Transform* parent)
	{
        // ENTITY
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
                // Type
                uint32_t type = 0;
                stream->Read(&type);

                // ID
                uint32_t id = 0;
                stream->Read(&id);

                // Add component
                if (!AddComponent(static_cast<ComponentType>(type), id))
                {
                    LOGF_ERROR("Component with an id of %d and a type of %d failed to load", id, type);
                }
            }

            // Sometimes there are component dependencies, e.g. a collider that needs
            // to set it's shape to a rigibody. So, it's important to first create all 
            // the components (like above) and then deserialize them (like here).
            auto components = GetAllComponents();
            for (const auto& component : components)
            {
                component->Deserialize(stream);
            }
        }

        // Set the transform's parent
        if (m_transform)
        {
            m_transform->SetParent(parent);
        }

		// CHILDREN
        {
            // 1st - children count
            const auto children_count = stream->ReadAs<uint32_t>();

            // 2nd - children IDs
            vector<std::shared_ptr<Entity>> children;
            for (uint32_t i = 0; i < children_count; i++)
            {
                uint32_t id             = stream->ReadAs<uint32_t>();
                uint32_t transform_id   = stream->ReadAs<uint32_t>();
                children.emplace_back(m_world->EntityCreate(id, transform_id));
            }

            // 3rd - children
            for (const auto& child : children)
            {
                child->Deserialize(stream, GetTransform_PtrRaw());
            }
        }

		if (m_transform)
		{
			m_transform->AcquireChildren();
		}

		// Make the scene resolve
		FIRE_EVENT(Event_World_Resolve);
	}

    void Entity::SetActive(const bool active)
    {
        m_is_active = active;

        m_world->IterateManagers([&](auto& manager)
        {
            auto components = manager->GetComponents(GetId());
            for (auto& component : components)
            {
                component->SetIsParentEntityActive(active);
            }
        });
    }

    shared_ptr<IComponent> Entity::AddComponent(const ComponentType type, uint32_t component_id /*= 0*/)
    {
        // This is the only hardcoded part regarding components. It's 
        // one function but it would be nice if that gets automated too, somehow...
        shared_ptr<IComponent> component;
        switch (type)
        {
            case ComponentType_AudioListener:	component = AddComponent<AudioListener>(component_id);  break;
            case ComponentType_AudioSource:		component = AddComponent<AudioSource>(component_id);	break;
            case ComponentType_Camera:			component = AddComponent<Camera>(component_id);		    break;
            case ComponentType_Collider:		component = AddComponent<Collider>(component_id);		break;
            case ComponentType_Constraint:		component = AddComponent<Constraint>(component_id);	    break;
            case ComponentType_Light:			component = AddComponent<Light>(component_id);		    break;
            case ComponentType_Renderable:		component = AddComponent<Renderable>(component_id);	    break;
            case ComponentType_RigidBody:		component = AddComponent<RigidBody>(component_id);	    break;
            case ComponentType_Script:			component = AddComponent<Script>(component_id);		    break;
            case ComponentType_Skybox:			component = AddComponent<Skybox>(component_id);		    break;
            case ComponentType_Transform:		component = AddComponent<Transform>(component_id);	    break;
            case ComponentType_Unknown:														            break;
            default:																		            break;
        }

        return component;
    }

    void Entity::RemoveComponent(const uint32_t id)
	{
        // Remove component mask
        m_component_mask &= ~(1 << static_cast<unsigned int>(m_id_to_type[id]));

        // Remove component from manager
        m_world->IterateManagers([&](auto& manager)
        {
            if (manager->m_type == m_id_to_type[id])
            {
                manager->RemoveComponentByID(GetId(), id);
            }
        });

		// Make the scene resolve
		FIRE_EVENT(Event_World_Resolve);
	}

    vector<shared_ptr<IComponent>> Entity::GetAllComponents() const
    {
        vector<shared_ptr<IComponent>> components;

        m_world->IterateManagers([&](auto& manager)
        {
            auto _components = manager->GetComponents(GetId());
            components.insert(components.end(), _components.begin(), _components.end());
        });

        return components;
    }
}
