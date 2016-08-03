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

//= INCLUDES ==========================
#include "MeshPool.h"
#include "../Graphics/GraphicsDevice.h"
#include "../Pools/MaterialPool.h"
//=====================================

class GameObject;
#define NULL_GAMEOBJECT_ID "-1"

class GameObjectPool
{
public:
	GameObjectPool();
	~GameObjectPool();

	static GameObjectPool& GetInstance()
	{
		static GameObjectPool instance;
		return instance;
	}

	void Initialize(GraphicsDevice* d3d11Device, Scene* scene, MeshPool* meshPool, MaterialPool* materialPool, TexturePool* texturePool, ShaderPool* shaderPool, PhysicsWorld* physics, ScriptEngine* scriptEngine);
	void Update();
	void Release();
	void DeleteAll();

	/*------------------------------------------------------------------------------
									[I/O]
	------------------------------------------------------------------------------*/
	void Serialize();
	void Deserialize();

	/*------------------------------------------------------------------------------
									[MISC]
	------------------------------------------------------------------------------*/
	std::vector<GameObject*> GetAllGameObjects();
	std::vector<GameObject*> GetRootGameObjects();
	GameObject* GetGameObjectRoot(GameObject* gameObject);
	int GetGameObjectCount();
	int GetGameObjectIndex(GameObject* gameObject);
	GameObject* GetGameObjectByName(std::string name);
	GameObject* GetGameObjectByIndex(int index);
	GameObject* GetGameObjectByID(std::string ID);
	std::vector<GameObject*> GetGameObjectsByParentID(std::string ID);
	bool GameObjectExists(GameObject* gameObject);
	bool GameObjectExistsByName(std::string name);
	void RemoveGameObject(GameObject* gameObject);
	void RemoveSingleGameObject(GameObject* gameObject);

	/*------------------------------------------------------------------------------
								[CALLED BY GAMEOBJECTS]
	------------------------------------------------------------------------------*/
	void AddGameObjectToPool(GameObject* gameObject);

private:
	std::vector<GameObject*> m_gameObjectPool;

	GraphicsDevice* m_graphicsDevice;
	Scene* m_scene;
	MeshPool* m_meshPool;
	MaterialPool* m_materialPool;
	TexturePool* m_texturePool;
	ShaderPool* m_shaderPool;
	PhysicsWorld* m_physics;
	ScriptEngine* m_scriptEngine;
};
