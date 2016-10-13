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

//= INCLUDES =====================
#include <vector>
#include "GameObject.h"
#include "../Math/Vector3.h"
#include "../Pools/TexturePool.h"
#include "../Pools/MaterialPool.h"
#include "../Pools/MeshPool.h"
#include "../Pools/ThreadPool.h"
//================================

#define SCENE_EXTENSION ".directus"

class Renderer;
class ModelImporter;

class Scene
{
public:
	Scene(TexturePool* texturePool, 
		MaterialPool* materialPool, 
		MeshPool* meshPool, ScriptEngine* 
		scriptEngine, PhysicsWorld* physics, 
		ModelImporter* modelLoader, 
		Renderer* renderer, 
		ShaderPool* shaderPool,
		ThreadPool* threadPool
	);
	~Scene();

	void Initialize();
	void Update();

	/*------------------------------------------------------------------------------
										[I/O]
	------------------------------------------------------------------------------*/
	bool SaveToFile(const std::string& filePath);
	bool LoadFromFile(const std::string& filePath);

	/*------------------------------------------------------------------------------
										[MISC]
	------------------------------------------------------------------------------*/
	void Clear();
	GameObject* GetSkybox();
	GameObject* GetMainCamera();
	void SetAmbientLight(float x, float y, float z);
	Directus::Math::Vector3 GetAmbientLight();
	void AnalyzeGameObjects();

private:
	// GAMEOBJECT CREATION =======================
	GameObject* CreateSkybox();
	GameObject* CreateCamera();
	GameObject* CreateDirectionalLight();
	//============================================

	std::vector<GameObject*> m_renderables;
	std::vector<GameObject*> m_lightsDirectional;
	std::vector<GameObject*> m_lightsPoint;

	GameObject* m_mainCamera;
	Directus::Math::Vector3 m_ambientLight;

	// DEPENDENCIES ==============================
	TexturePool* m_texturePool;
	MaterialPool* m_materialPool;
	MeshPool* m_meshPool;
	ScriptEngine* m_scriptEngine;
	PhysicsWorld* m_physics;
	ModelImporter* m_modelLoader;
	Renderer* m_renderer;
	ShaderPool* m_shaderPool;
	ThreadPool* m_threadPool;
	//============================================
};
