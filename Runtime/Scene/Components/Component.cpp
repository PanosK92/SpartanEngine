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

//= INCLUDES ========================
#include "Component.h"
#include "Skybox.h"
#include "Script.h"
#include "RigidBody.h"
#include "MeshRenderer.h"
#include "MeshFilter.h"
#include "LineRenderer.h"
#include "Constraint.h"
#include "Collider.h"
#include "Camera.h"
#include "AudioSource.h"
#include "AudioListener.h"
#include "Light.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../../Core/Context.h"
#include "../../Core/GUIDGenerator.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	void Component::Register(GameObject* gameObject, Transform* transform, Context* context, ComponentType type)
	{
		m_enabled		= true;
		m_gameObject	= gameObject;
		m_transform		= transform;
		m_context		= context;
		m_type			= type;
		m_ID			= GENERATE_GUID;
	}

	weak_ptr<GameObject> Component::GetGameObjectRef()
	{
		if (!m_context)
			return weak_ptr<GameObject>();

		return m_context->GetSubsystem<Scene>()->GetWeakReferenceToGameObject(m_gameObject);
	}

	const string& Component::GetGameObjectName()
	{
		if (!m_gameObject)
			return "";

		return m_gameObject->GetName();
	}

	template <typename T>
	ComponentType Component::ToComponentType()
	{
		if (typeid(T) == typeid(AudioListener))
			return ComponentType_AudioListener;

		if (typeid(T) == typeid(AudioSource))
			return ComponentType_AudioSource;

		if (typeid(T) == typeid(Camera))
			return ComponentType_Camera;

		if (typeid(T) == typeid(Collider))
			return ComponentType_Collider;

		if (typeid(T) == typeid(Constraint))
			return ComponentType_Constraint;

		if (typeid(T) == typeid(Light))
			return ComponentType_Light;

		if (typeid(T) == typeid(LineRenderer))
			return ComponentType_LineRenderer;

		if (typeid(T) == typeid(MeshFilter))
			return ComponentType_MeshFilter;

		if (typeid(T) == typeid(MeshRenderer))
			return ComponentType_MeshRenderer;

		if (typeid(T) == typeid(RigidBody))
			return ComponentType_RigidBody;

		if (typeid(T) == typeid(Script))
			return ComponentType_Script;

		if (typeid(T) == typeid(Skybox))
			return ComponentType_Skybox;

		if (typeid(T) == typeid(Transform))
			return ComponentType_Transform;

		return ComponentType_Unknown;
	}

#define INSTANTIATE(T) template ENGINE_CLASS ComponentType Component::ToComponentType<T>()
	// Explicit template instantiation
	INSTANTIATE(AudioListener);
	INSTANTIATE(AudioSource);
	INSTANTIATE(Camera);
	INSTANTIATE(Collider);
	INSTANTIATE(Constraint);
	INSTANTIATE(Light);
	INSTANTIATE(LineRenderer);
	INSTANTIATE(MeshFilter);
	INSTANTIATE(MeshRenderer);
	INSTANTIATE(RigidBody);
	INSTANTIATE(Script);
	INSTANTIATE(Skybox);
	INSTANTIATE(Transform);
}
