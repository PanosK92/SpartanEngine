/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "Skybox.h"
#include "Script.h"
#include "RigidBody.h"
#include "Renderable.h"
#include "LineRenderer.h"
#include "Constraint.h"
#include "Collider.h"
#include "Camera.h"
#include "AudioSource.h"
#include "AudioListener.h"
#include "Light.h"
#include "Transform.h"
#include "../Actor.h"
#include "../../Core/Context.h"
#include "../../Core/GUIDGenerator.h"
#include "../../FileSystem/FileSystem.h"
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	IComponent::IComponent(Context* context, Actor* actor, Transform* transform)
	{
		m_context		= context;
		m_actor			= actor;
		m_transform		= transform;
		m_enabled		= true;
		m_ID			= GENERATE_GUID;
	}

	shared_ptr<Actor> IComponent::Getactor_PtrShared()
	{
		return m_actor->GetPtrShared();
	}

	const string& IComponent::GetActorName()
	{
		if (!m_actor)
			return NOT_ASSIGNED;

		return m_actor->GetName();
	}

	template <typename T>
	ComponentType IComponent::Type_To_Enum() { return ComponentType_Unknown; }
	// Explicit template instantiation
	#define REGISTER_COMPONENT(T, enumT) template<> ENGINE_CLASS ComponentType IComponent::Type_To_Enum<T>() { return enumT; }
	// To add a new component to the engine, simply register it here
	REGISTER_COMPONENT(AudioListener,	ComponentType_AudioListener)
	REGISTER_COMPONENT(AudioSource,		ComponentType_AudioSource)
	REGISTER_COMPONENT(Camera,			ComponentType_Camera)
	REGISTER_COMPONENT(Collider,		ComponentType_Collider)
	REGISTER_COMPONENT(Constraint,		ComponentType_Constraint)
	REGISTER_COMPONENT(Light,			ComponentType_Light)
	REGISTER_COMPONENT(LineRenderer,	ComponentType_LineRenderer)
	REGISTER_COMPONENT(Renderable,		ComponentType_Renderable)
	REGISTER_COMPONENT(RigidBody,		ComponentType_RigidBody)
	REGISTER_COMPONENT(Script,			ComponentType_Script)
	REGISTER_COMPONENT(Skybox,			ComponentType_Skybox)
	REGISTER_COMPONENT(Transform,		ComponentType_Transform)
}
