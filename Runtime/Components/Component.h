/*
Copyright(c) 2016-2017 Panos Karabelas

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

#pragma once

//= INCLUDES ==============
#include <memory>
#include <string>
#include "../Core/EngineDefs.h"
//=========================

namespace Directus
{
	class GameObject;
	class Transform;
	class Context;
	class FileStream;

	enum ComponentType : unsigned int
	{
		ComponentType_AudioListener,
		ComponentType_AudioSource,
		ComponentType_Camera,
		ComponentType_Collider,
		ComponentType_Constraint,
		ComponentType_Light,
		ComponentType_LineRenderer,
		ComponentType_MeshFilter,
		ComponentType_MeshRenderer,
		ComponentType_RigidBody,
		ComponentType_Script,
		ComponentType_Skybox,
		ComponentType_Transform,
		ComponentType_Unknown
	};

	class ENGINE_CLASS Component
	{
	public:
		virtual ~Component() {}

		// Runs when the component is first created
		void Register(GameObject* gameObject, Transform* transform, Context* context, ComponentType type);

		// Runs when the component gets added
		virtual void Initialize() {}

		// Runs every time the simulation starts
		virtual void Start() {}

		// Runs every time the simulation stops
		virtual void OnDisable() {}

		// Runs when the component is removed
		virtual void Remove(){}

		// Runs every frame
		virtual void Update() {}

		// Runs when the GameObject is being saved
		virtual void Serialize(FileStream* stream) {}

		// Runs when the GameObject is being loaded
		virtual void Deserialize(FileStream* stream) {}
		
		//= PROPERTIES =====================================
		GameObject* GetGameObject() { return m_gameObject; }
		std::weak_ptr<GameObject> GetGameObjectRef();
		Transform* GetTransform() { return m_transform; }
		Context* GetContext() { return m_context; }
		unsigned int GetID() { return m_ID; }
		void SetID(unsigned int id) { m_ID = id; }
		ComponentType GetType() { return m_type; }
		//==================================================

		//= HELPER FUNCTIONS ==================
		const std::string& GetGameObjectName();

		template <typename T>
		static ComponentType ToComponentType();
		//=====================================

	protected:
		// The type of the component
		ComponentType m_type		= ComponentType_Unknown;
		// The id of the component
		unsigned int m_ID			= 0;
		// The state of the component
		bool m_enabled				= false;
		// The owner of the component
		GameObject* m_gameObject	= nullptr;
		// The transform of the component (always exists)
		Transform* m_transform		= nullptr;
		// The context of the engine
		Context* m_context			= nullptr;
	};
}