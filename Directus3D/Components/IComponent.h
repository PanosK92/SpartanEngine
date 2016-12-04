/*
Copyright(c) 2016 Panos Karabelas

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
#include "../Core/Helper.h"
//=========================

class GameObject;
class Transform;
class IGraphicsDevice;
class Scene;
class Renderer;
class PhysicsWorld;
class ScriptEngine;
class MeshPool;
class MaterialPool;
class TexturePool;
class ShaderPool;
class Context;

class DllExport IComponent
{
public:
	virtual ~IComponent(){}

	// Runs when the component gets added
	virtual void Reset() = 0;
	//virtual void Awake() = 0; // todo
	//virtual void OnEnable() = 0; // todo
	// Runs everytime the simulation starts
	virtual void Start() = 0;
	// Runs everytime the simulation stops
	virtual void OnDisable() = 0;
	// Runs when the component is removed
	virtual void Remove() = 0;
	// Runs every frame
	virtual void Update() = 0;
	// Runs when the GameObject is being saved
	virtual void Serialize() = 0;
	// Runs when the GameObject is being loaded
	virtual void Deserialize() = 0;

	//= PROPERTIES ================================
	std::string g_ID;
	bool g_enabled;
	// The GameObject the component is attached to
	GameObject* g_gameObject;
	// The only always existing component
	Transform* g_transform;
	// The engine contect
	Context* g_context;
	//=============================================
};
