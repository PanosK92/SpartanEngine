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

//= INCLUDES ====
#include <string>
//===============

class GameObject;
class Transform;
class GraphicsDevice;
class Scene;
class PhysicsWorld;
class ScriptEngine;
class MeshPool;
class MaterialPool;
class TexturePool;
class ShaderPool;

class __declspec(dllexport) IComponent
{
public:
	virtual ~IComponent(){}

	virtual void Initialize() = 0;
	virtual void Remove() = 0;
	virtual void Update() = 0;
	virtual void Serialize() = 0;
	virtual void Deserialize() = 0;

	//= PROPERTIES ===================
	std::string g_ID;
	bool g_enabled;
	//================================

	//= SOME USEFUL POINTERS =========
	GameObject* g_gameObject;
	Transform* g_transform;
	GraphicsDevice* g_graphicsDevice;
	Scene* g_scene;
	MeshPool* g_meshPool;
	MaterialPool* g_materialPool;
	PhysicsWorld* g_physicsWorld;
	ScriptEngine* g_scriptEngine;
	TexturePool* g_texturePool;
	ShaderPool* g_shaderPool;
	//================================
};
