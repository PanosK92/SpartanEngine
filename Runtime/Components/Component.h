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

//= INCLUDES =====================
#include <memory>
#include "../Core/Helper.h"
#include "../Core/GUIDGenerator.h"
//================================

namespace Directus
{
	class AudioListener;
	class AudioSource;
	class Camera;
	class Collider;
	class Constraint;
	class Light;
	class LineRenderer;
	class MeshFilter;
	class MeshRenderer;
	class RigidBody;
	class Script;
	class Skybox;
	class Transform;
	class GameObject;
	class Transform;
	class IGraphicsDevice;
	class Scene;
	class Renderer;
	class Physics;
	class Scripting;
	class MeshPool;
	class MaterialPool;
	class TexturePool;
	class ShaderPool;
	class Context;
	class FileStream;

	// Add new components here
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
	// Add new components here
	template <class T>
	static ComponentType ToComponentType()
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

	class DLL_API Component
	{
	public:
		virtual ~Component() {}

		// Runs when the component gets added
		virtual void Initialize() = 0;

		// Runs every time the simulation starts
		virtual void Start() = 0;

		// Runs every time the simulation stops
		virtual void OnDisable() = 0;

		// Runs when the component is removed
		virtual void Remove() = 0;

		// Runs every frame
		virtual void Update() = 0;

		// Runs when the GameObject is being saved
		virtual void Serialize(FileStream* stream) = 0;

		// Runs when the GameObject is being loaded
		virtual void Deserialize(FileStream* stream) = 0;

		// Should be called by the derived component to register it's type
		void Register(ComponentType type)
		{
			g_type = type;
			g_ID = GENERATE_GUID;
		}

		//= PROPERTIES ========================
		ComponentType g_type;
		unsigned int g_ID;
		bool g_enabled;
		// The component owner
		std::weak_ptr<GameObject> g_gameObject;
		// The only always existing component
		Transform* g_transform;
		// The engine context
		Context* g_context;	
		//=====================================
	};
}