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

//= INCLUDES ===========================
#include "IComponent.h"
#include "Light.h"
#include "Skybox.h"
#include "Script.h"
#include "RigidBody.h"
#include "Collider.h"
#include "Constraint.h"
#include "Camera.h"
#include "AudioSource.h"
#include "AudioListener.h"
#include "Renderable.h"
#include "Transform.h"
#include "../Entity.h"
#include "../../FileSystem/FileSystem.h"
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    IComponent::IComponent(Context* context, Entity* entity, Transform* transform /*= nullptr*/) : Spartan_Object()
    {
        m_context   = context;
        m_entity    = entity;
        m_transform = transform == nullptr ? entity->GetTransform_PtrRaw() : transform;
        m_enabled   = true;
    }

    shared_ptr<Entity> IComponent::GetEntity_PtrShared() const
	{
		return m_entity->GetPtrShared();
	}

	string IComponent::GetEntityName() const
	{
		if (!m_entity)
			return "";

		return m_entity->GetName();
	}

	template <typename T>
    inline constexpr ComponentType IComponent::TypeToEnum() { return ComponentType_Unknown; }

    template<typename T>
    inline constexpr void validate_component_type() { static_assert(std::is_base_of<IComponent, T>::value, "Provided type does not implement IComponent"); }

	// Explicit template instantiation
	#define REGISTER_COMPONENT(T, enumT) template<> SPARTAN_CLASS ComponentType IComponent::TypeToEnum<T>() { validate_component_type<T>(); return enumT; }

	// To add a new component to the engine, simply register it here
	REGISTER_COMPONENT(AudioListener,	ComponentType_AudioListener)
	REGISTER_COMPONENT(AudioSource,		ComponentType_AudioSource)
	REGISTER_COMPONENT(Camera,			ComponentType_Camera)
	REGISTER_COMPONENT(Collider,		ComponentType_Collider)
	REGISTER_COMPONENT(Constraint,		ComponentType_Constraint)
	REGISTER_COMPONENT(Light,			ComponentType_Light)
	REGISTER_COMPONENT(Renderable,		ComponentType_Renderable)
	REGISTER_COMPONENT(RigidBody,		ComponentType_RigidBody)
	REGISTER_COMPONENT(Script,			ComponentType_Script)
	REGISTER_COMPONENT(Skybox,			ComponentType_Skybox)
	REGISTER_COMPONENT(Transform,		ComponentType_Transform)
}
