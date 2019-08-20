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
    Entity::Entity(Context* context, uint32_t id /*= 0*/, uint32_t transform_id /*= 0*/, bool postpone_transform) : Spartan_Object(id)
    {
        m_context   = context;
        m_world     = context->GetSubsystem<World>().get();
        m_transform_id = transform_id;

        if (!postpone_transform)
            this->AddComponent<Transform>(transform_id, false); // All entities must have a transform
    }

    Entity::~Entity()
	{
		// delete components
        m_world->IterateManagers([&](auto& manager)
        {
            if (auto& components = manager->GetComponents(GetId()); !components.empty())
            {
                for (auto& component : components)
                {
                    if (component != nullptr)
                        component->OnRemove();
                    manager->RemoveComponent(GetId());
                }
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
            stream->Write(m_component_mask);
            m_context->GetSubsystem<World>()->IterateManagers([&](std::shared_ptr<BaseComponentManager> manager)
            {
                    bool hasComponent = m_component_mask & (1 << manager->m_type);
                    stream->Write(hasComponent);

                    if (hasComponent)
                    {
                        std::shared_ptr<IComponent> _component = manager->GetComponent(GetId());                        

                        stream->Write(static_cast<uint32_t>(_component->GetType()));
                        stream->Write(_component->GetId());                    
                    }
                    else
                    {
                        // Dummies
                        stream->Write(-1);
                        stream->Write(-2);
                    }
            });

            m_context->GetSubsystem<World>()->IterateManagers([&](std::shared_ptr<BaseComponentManager> manager)
            {
                  std::shared_ptr<IComponent> _component = manager->GetComponent(GetId());

                  if (_component != nullptr)
                  {                   
                    _component->Serialize(stream);
                  }                       
                    
            });
        }
        

		// CHILDREN
        {
            const auto children = GetComponent<Transform>()->GetChildren();

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
                child->GetEntity_PtrShared()->Serialize(stream);
            }
        }
	}

	void Entity::Deserialize(FileStream* stream, Transform* parent)
	{
        uint32_t id;

        // ENTITY
        {
            stream->Read(&m_is_active);
            stream->Read(&m_hierarchy_visibility);
            stream->Read(&id);
            stream->Read(&m_name);
        }
        
        // COMPONENTS
        {
            stream->Read(&m_component_mask);
            m_context->GetSubsystem<World>()->IterateManagers([&](std::shared_ptr<BaseComponentManager> manager)
            {
                    bool hasComponent;
                    stream->Read(&hasComponent);

                    if (hasComponent)
                    {
                        uint32_t type;
                        stream->Read(&type);

                        uint32_t id;
                        stream->Read(&id);                       

                       auto componentType = static_cast<ComponentType>(type);
                       auto _component = AddComponent(componentType, id, false); // Add component at all costs and override component mask checks

                       // Add component
                       if (_component == nullptr)
                       {
                           LOGF_ERROR("Component with an id of %d and a type of %d failed to load", id, type);
                       }                      
                    }
                    else
                    {
                        // Dummies
                        int id, type;
                        stream->Read(&id);
                        stream->Read(&type);
                    }
            });
            // Sometimes there are component dependencies, e.g. a collider that needs
            // to set it's shape to a rigibody. So, it's important to first create all 
            // the components (like above) and then deserialize them (like here).

            m_context->GetSubsystem<World>()->IterateManagers([&](std::shared_ptr<BaseComponentManager> manager)
            {
                 std::shared_ptr<IComponent> _component = manager->GetComponent(GetId());

                 if (_component != nullptr) 
                     _component->Deserialize(stream);                 
                 else
                     LOGF_INFO("Component is nullptr");
            });
        }
        

        // Set the transform's parent
        if (m_transform && (parent != NULL))
        {
            m_transform->SetParent(parent);
        }

		// CHILDREN
        {
            // 1st - children count
            int children_count = 0;
           stream->Read(&children_count);
            
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

        auto transform = GetComponent<Transform>();
		if (transform)
		{
			transform->AcquireChildren();
		}

		// Make the scene resolve
		FIRE_EVENT(Event_World_Resolve);
	}

	std::shared_ptr<IComponent> Entity::AddComponent(ComponentType type, uint32_t component_id, bool check_if_exists)
    {
        // This is the only hardcoded part regarding components. It's 
        // one function but it would be nice if that gets automated too, somehow...
        shared_ptr<IComponent> component;
        switch (type)
        {
        case ComponentType_AudioListener:	component = AddComponent<AudioListener>(component_id, check_if_exists);  break;
        case ComponentType_AudioSource:		component = AddComponent<AudioSource>(component_id, check_if_exists);	 break;
        case ComponentType_Camera:			component = AddComponent<Camera>(component_id, check_if_exists);		 break;
        case ComponentType_Collider:		component = AddComponent<Collider>(component_id, check_if_exists);		 break;
        case ComponentType_Constraint:		component = AddComponent<Constraint>(component_id, check_if_exists);     break;
        case ComponentType_Light:			component = AddComponent<Light>(component_id, check_if_exists);		     break;
        case ComponentType_Renderable:		component = AddComponent<Renderable>(component_id, check_if_exists);	 break;
        case ComponentType_RigidBody:		component = AddComponent<RigidBody>(component_id, check_if_exists);	     break;
        case ComponentType_Script:			component = AddComponent<Script>(component_id, check_if_exists);		 break;
        case ComponentType_Skybox:			component = AddComponent<Skybox>(component_id, check_if_exists);		 break;
        case ComponentType_Transform:		component = AddComponent<Transform>(component_id, check_if_exists);	     break;
        case ComponentType_Unknown:														                             break;
        default:																		                             break;
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
            auto _component = manager->GetComponent(GetId());
            components.push_back(_component);
        });

        return components;
    }
}
