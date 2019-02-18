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
#include "../IO/FileStream.h"
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
#include "../Core/GUIDGenerator.h"
//============================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	Entity::Entity(Context* context)
	{
		m_context				= context;
		m_ID					= GENERATE_GUID;
		m_name					= "Entity";
		m_isActive				= true;
		m_hierarchyVisibility	= true;	
	}

	Entity::~Entity()
	{
		// delete components
		for (auto it = m_components.begin(); it != m_components.end();)
		{
			(*it)->OnRemove();
			(*it).reset();
			it = m_components.erase(it);
		}
		m_components.clear();

		m_name.clear();
		m_ID					= NOT_ASSIGNED_HASH;
		m_isActive				= true;
		m_hierarchyVisibility	= true;
	}

	void Entity::Initialize(Transform* transform)
	{
		m_transform = transform;
	}

	void Entity::Clone()
	{
		auto scene = m_context->GetSubsystem<World>();
		vector<Entity*> clones;

		// Creation of new entity and copying of a few properties
		auto Cloneentity = [&scene, &clones](Entity* entity)
		{
			// Clone the name and the ID
			Entity* clone = scene->Entity_Create().get();
			clone->SetID(GENERATE_GUID);
			clone->SetName(entity->GetName());
			clone->SetActive(entity->IsActive());
			clone->SetHierarchyVisibility(entity->IsVisibleInHierarchy());

			// Clone all the components
			for (const auto& component : entity->GetAllComponents())
			{
				shared_ptr<IComponent> originalComp = component;
				shared_ptr<IComponent> cloneComp	= clone->AddComponent(component->GetType());
				cloneComp->SetAttributes(originalComp->GetAttributes());
			}

			clones.emplace_back(clone);

			return clone;
		};

		// Cloning of an entity and it's descendants (this is a recursive lambda)
		function<Entity*(Entity*)> CloneentityAndDescendants = [&CloneentityAndDescendants, &Cloneentity, &clones](Entity* original)
		{
			// clone self
			Entity* cloneSelf = Cloneentity(original);

			// clone children make them call this lambda
			for (const auto& childTransform : original->GetTransform_PtrRaw()->GetChildren())
			{
				Entity* cloneChild = CloneentityAndDescendants(childTransform->GetEntity_PtrRaw());
				cloneChild->GetTransform_PtrRaw()->SetParent(cloneSelf->GetTransform_PtrRaw());
			}

			// return self
			return cloneSelf;
		};

		// Clone the entire hierarchy
		CloneentityAndDescendants(this);
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

	void Entity::Tick()
	{
		if (!m_isActive)
			return;

		// call component Update()
		for (const auto& component : m_components)
		{
			component->OnTick();
		}
	}

	void Entity::Serialize(FileStream* stream)
	{
		//= BASIC DATA ======================
		stream->Write(m_isActive);
		stream->Write(m_hierarchyVisibility);
		stream->Write(m_ID);
		stream->Write(m_name);
		//===================================

		//= COMPONENTS ================================
		stream->Write((int)m_components.size());
		for (const auto& component : m_components)
		{
			stream->Write((unsigned int)component->GetType());
			stream->Write(component->GetID());
		}

		for (const auto& component : m_components)
		{
			component->Serialize(stream);
		}
		//=============================================

		//= CHILDREN ==================================
		vector<Transform*> children = GetTransform_PtrRaw()->GetChildren();

		// 1st - children count
		stream->Write((int)children.size());

		// 2nd - children IDs
		for (const auto& child : children)
		{
			stream->Write(child->GetID());
		}

		// 3rd - children
		for (const auto& child : children)
		{
			if (child->GetEntity_PtrRaw())
			{
				child->GetEntity_PtrRaw()->Serialize(stream);
			}
			else
			{
				LOG_ERROR("Aborting , child entity is nullptr.");
				break;
			}
		}
		//=============================================
	}

	void Entity::Deserialize(FileStream* stream, Transform* parent)
	{
		//= BASIC DATA =====================
		stream->Read(&m_isActive);
		stream->Read(&m_hierarchyVisibility);
		stream->Read(&m_ID);
		stream->Read(&m_name);
		//==================================

		//= COMPONENTS ================================
		int componentCount = stream->ReadInt();
		for (int i = 0; i < componentCount; i++)
		{
			unsigned int type = ComponentType_Unknown;
			unsigned int id = 0;

			stream->Read(&type); // load component's type
			stream->Read(&id); // load component's id

			auto component = AddComponent((ComponentType)type);
			component->SetID(id);
		}
		// Sometimes there are component dependencies, e.g. a collider that needs
		// to set it's shape to a rigibody. So, it's important to first create all 
		// the components (like above) and then deserialize them (like here).
		for (const auto& component : m_components)
		{
			component->Deserialize(stream);
		}
		//=============================================

		// Set the transform's parent
		if (m_transform)
		{
			m_transform->SetParent(parent);
		}

		//= CHILDREN ===================================
		// 1st - children count
		int childrenCount = stream->ReadInt();

		// 2nd - children IDs
		auto scene = m_context->GetSubsystem<World>();
		vector<std::weak_ptr<Entity>> children;
		for (int i = 0; i < childrenCount; i++)
		{
			std::shared_ptr<Entity> child = scene->Entity_Create();
			child->SetID(stream->ReadUInt());
			children.emplace_back(child);
		}

		// 3rd - children
		for (const auto& child : children)
		{
			child.lock()->Deserialize(stream, GetTransform_PtrRaw());
		}
		//=============================================

		if (m_transform)
		{
			m_transform->AcquireChildren();
		}

		// Make the scene resolve
		FIRE_EVENT(Event_World_Resolve);
	}

	shared_ptr<IComponent> Entity::AddComponent(ComponentType type)
	{
		// This is the only hardcoded part regarding components. It's 
		// one function but it would be nice if that gets automated too, somehow...
		shared_ptr<IComponent> component;
		switch (type)
		{
			case ComponentType_AudioListener:	component = AddComponent<AudioListener>();	break;
			case ComponentType_AudioSource:		component = AddComponent<AudioSource>();	break;
			case ComponentType_Camera:			component = AddComponent<Camera>();			break;
			case ComponentType_Collider:		component = AddComponent<Collider>();		break;
			case ComponentType_Constraint:		component = AddComponent<Constraint>();		break;
			case ComponentType_Light:			component = AddComponent<Light>();			break;
			case ComponentType_Renderable:		component = AddComponent<Renderable>();		break;
			case ComponentType_RigidBody:		component = AddComponent<RigidBody>();		break;
			case ComponentType_Script:			component = AddComponent<Script>();			break;
			case ComponentType_Skybox:			component = AddComponent<Skybox>();			break;
			case ComponentType_Transform:		component = AddComponent<Transform>();		break;
			case ComponentType_Unknown:														break;
			default:																		break;
		}

		// Make the scene resolve
		FIRE_EVENT(Event_World_Resolve);

		return component;
	}

	void Entity::RemoveComponentByID(unsigned int id)
	{
		for (auto it = m_components.begin(); it != m_components.end(); ) 
		{
			auto component = *it;
			if (id == component->GetID())
			{
				component->OnRemove();
				component.reset();
				it = m_components.erase(it);
			}
			else
			{
				++it;
			}
		}

		// Make the scene resolve
		FIRE_EVENT(Event_World_Resolve);
	}
}
