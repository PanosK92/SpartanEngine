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

//= INCLUDES ========================
#include <map>
#include "../Components/IComponent.h"
#include <vector>
//===================================

class Transform;

class __declspec(dllexport) GameObject
{
public:
	GameObject();
	~GameObject();

	void Initialize(std::shared_ptr<Graphics> graphicsDevice, std::shared_ptr<Scene> scene, std::shared_ptr<Renderer> renderer, std::shared_ptr<MeshPool> meshPool, std::shared_ptr<MaterialPool> materialPool, std::shared_ptr<TexturePool> texturePool, std::shared_ptr<ShaderPool> shaderPool, std::shared_ptr<PhysicsWorld> physics, std::shared_ptr<ScriptEngine> scriptEngine);
	void Start();
	void Update();

	std::string GetName();
	void SetName(const std::string& name);

	std::string GetID();
	void SetID(const std::string& ID);

	void SetActive(bool active);
	bool IsActive();

	void SetHierarchyVisibility(bool value);
	bool IsVisibleInHierarchy();

	void Serialize();
	void Deserialize();

	//= Components ============================================
	template <class Type>
	Type* AddComponent();

	template <class Type>
	Type* GetComponent();

	template <class Type>
	std::vector<Type*> GetComponents();

	template <class Type>
	bool HasComponent();

	template <class Type>
	void RemoveComponent();

	void RemoveComponentByID(const std::string& id);
	//=========================================================

	Transform* GetTransform();
private:
	std::string m_ID;
	std::string m_name;
	bool m_isActive;
	bool m_hierarchyVisibility;
	std::multimap<std::string, IComponent*> m_components;

	// This is the only component that is guaranteed to be always attached,
	// it also is required by most systems in the engine, it's important to
	// keep a local copy of it here and avoid any runtime searching (performance).
	Transform* m_transform;

	std::shared_ptr<Graphics> m_graphics;
	std::shared_ptr<Scene> m_scene;
	std::shared_ptr<Renderer> m_renderer;
	std::shared_ptr<MeshPool> m_meshPool;
	std::shared_ptr<MaterialPool> m_materialPool;
	std::shared_ptr<TexturePool> m_texturePool;
	std::shared_ptr<ShaderPool> m_shaderPool;
	std::shared_ptr<PhysicsWorld> m_physics;
	std::shared_ptr<ScriptEngine> m_scriptEngine;

	//= HELPER FUNCTIONS ====================================
	IComponent* AddComponentBasedOnType(const std::string& typeStr);
};
