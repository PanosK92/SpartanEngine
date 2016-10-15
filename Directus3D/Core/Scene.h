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
#include "../Multithreading/ThreadPool.h"
//================================

#define SCENE_EXTENSION ".directus"

class Renderer;
class ModelImporter;

class Scene
{
public:
	Scene(std::shared_ptr<TexturePool> texturePool, 
		std::shared_ptr<MaterialPool> materialPool, 
		std::shared_ptr<MeshPool> meshPool, std::shared_ptr<ScriptEngine> 
		scriptEngine, std::shared_ptr<PhysicsWorld> physics, 
		std::shared_ptr<ModelImporter> modelLoader, 
		std::shared_ptr<Renderer> renderer, 
		std::shared_ptr<ShaderPool> shaderPool,
		std::shared_ptr<ThreadPool> threadPool
	);
	~Scene();

	void Initialize();
	void Update();

	/*------------------------------------------------------------------------------
										[I/O]
	------------------------------------------------------------------------------*/
	void SaveToFileAsync(const std::string& filePath);
	void LoadFromFileAsync(const std::string& filePath);
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
	std::shared_ptr<TexturePool> m_texturePool;
	std::shared_ptr<MaterialPool> m_materialPool;
	std::shared_ptr<MeshPool> m_meshPool;
	std::shared_ptr<ScriptEngine> m_scriptEngine;
	std::shared_ptr<PhysicsWorld> m_physics;
	std::shared_ptr<ModelImporter> m_modelLoader;
	std::shared_ptr<Renderer> m_renderer;
	std::shared_ptr<ShaderPool> m_shaderPool;
	std::shared_ptr<ThreadPool> m_threadPool;
	//============================================
};
