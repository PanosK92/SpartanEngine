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
#include <string>
#include <memory>
#include "../Core/Helper.h"
#include "../Core/GUIDGenerator.h"

//=========================

namespace Directus
{
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

	class DLL_API IComponent
	{
	public:
		virtual ~IComponent() {}

		// Runs when the component gets added
		virtual void Reset() = 0;
		//virtual void Awake() = 0; // todo
		//virtual void OnEnable() = 0; // todo
		// Runs every time the simulation starts
		virtual void Start() = 0;
		// Runs every time the simulation stops
		virtual void OnDisable() = 0;
		// Runs when the component is removed
		virtual void Remove() = 0;
		// Runs every frame
		virtual void Update() = 0;
		// Runs when the GameObject is being saved
		virtual void Serialize() = 0;
		// Runs when the GameObject is being loaded
		virtual void Deserialize() = 0;

		// Should be called by the derived component to register it's type
		void Register()
		{
			// Convert class Type to a string.
			g_type = typeid(*this).name();
			// class Directus::Transform -> Transform
			g_type = g_type.substr(g_type.find_last_of(":") + 1);

			g_ID = GENERATE_GUID;
		}

		//= PROPERTIES ================================
		std::string g_ID;	
		std::string g_type;
		bool g_enabled;
		// The GameObject the component is attached to
		std::weak_ptr<GameObject> g_gameObject;
		// The only always existing component
		Transform* g_transform;
		// The engine context
		Context* g_context;	
		//=============================================
	};
}