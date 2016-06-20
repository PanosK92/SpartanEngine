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

//= INCLUDES ==========================
#include <fstream>
#include "Scene.h"
#include <complex>
#include "../IO/Serializer.h"
#include "../Pools/GameObjectPool.h"
#include "../IO/FileHelper.h"
#include "../IO/Log.h"
#include "../Core/Settings.h"
#include "../Graphics//Renderer.h"
#include "../Components/Transform.h"
#include "../Components/MeshRenderer.h"
#include "../Components/Camera.h"
#include "../Components/LineRenderer.h"
#include "../Components/Skybox.h"
#include "../Components/Script.h"
#include "../Physics/PhysicsEngine.h"
//=====================================

using namespace Directus::Math;

Scene::Scene(TexturePool* texturePool, MaterialPool* materialPool, MeshPool* meshPool, ScriptEngine* scriptEngine, PhysicsEngine* physics, ModelLoader* modelLoader, Renderer* renderer)
{
	m_renderer = renderer;
	m_texturePool = texturePool;
	m_materialPool = materialPool;
	m_meshPool = meshPool;
	m_scriptEngine = scriptEngine;
	m_physics = physics;
	m_modelLoader = modelLoader;

	m_mainCamera = nullptr;
	m_isDirty = true;
}

Scene::~Scene()
{
	Clear();
}

void Scene::Initialize()
{
	m_ambientLight = Vector3::Zero;
	m_mainCamera = CreateCamera();
	CreateDirectionalLight();
	m_isDirty = true;
}

void Scene::Update()
{
	if (!m_isDirty)
		return;

	AnalyzeGameObjects();

	m_isDirty = false;
}

/*------------------------------------------------------------------------------
										[I/O]
------------------------------------------------------------------------------*/
bool Scene::SaveToFile(std::string path)
{
	Serializer::StartWriting(path);
	//================================================
	m_texturePool->Serialize();
	m_materialPool->Serialize();
	m_meshPool->Serialize();
	GameObjectPool::GetInstance().Serialize();
	//================================================
	Serializer::StopWriting();

	return true;
}

bool Scene::LoadFromFile(std::string path)
{
	if (!FileHelper::FileExists(path))
	{
		LOG(path + " was not found.", Log::Error);
		return false;
	}
	Clear();

	// if we are in EDITOR mode and the scene 
	// is in Editor_Play, reset it to Editor_Debug.
	if (ENGINE_MODE == Editor_Play)
		Settings::GetInstance().SetEngineMode(Editor_Debug);

	Serializer::StartReading(path);
	//===========================================
	m_texturePool->Deserialize();
	m_materialPool->Deserialize();
	m_meshPool->Deserialize();
	GameObjectPool::GetInstance().Deserialize();
	//===========================================
	Serializer::StopReading();

	m_isDirty = true;

	return true;
}

/*------------------------------------------------------------------------------
									[MISC]
------------------------------------------------------------------------------*/
void Scene::Clear()
{
	m_renderablePool.clear();
	m_renderablePool.shrink_to_fit();

	m_directionalLightPool.clear();
	m_directionalLightPool.shrink_to_fit();

	m_pointLightPool.clear();
	m_pointLightPool.shrink_to_fit();

	m_mainCamera = nullptr;

	m_texturePool->Clear();
	m_meshPool->Clear();
	m_materialPool->Clear();
	GameObjectPool::GetInstance().Clear();
	m_scriptEngine->Reset();
	m_physics->Reset();
}

GameObject* Scene::GetMainCamera()
{
	return m_mainCamera;
}

void Scene::SetAmbientLight(float x, float y, float z)
{
	m_ambientLight = Vector3(x, y, z);
}

Vector3 Scene::GetAmbientLight()
{
	return m_ambientLight;
}

void Scene::MakeDirty()
{
	m_isDirty = true;
	m_renderer->MakeDirty();
}

std::vector<GameObject*> Scene::GetRenderables()
{
	return m_renderablePool;
}

std::vector<GameObject*> Scene::GetDirectionalLights()
{
	return m_directionalLightPool;
}

std::vector<GameObject*> Scene::GetPointLights()
{
	return m_pointLightPool;
}

/*------------------------------------------------------------------------------
							[SCENE ANALYSIS]
------------------------------------------------------------------------------*/
void Scene::AnalyzeGameObjects()
{
	m_renderablePool.clear();
	m_renderablePool.shrink_to_fit();

	m_directionalLightPool.clear();
	m_directionalLightPool.shrink_to_fit();

	m_pointLightPool.clear();
	m_pointLightPool.shrink_to_fit();

	std::vector<GameObject*> gameObjects = GameObjectPool::GetInstance().GetAllGameObjects();
	for (int i = 0; i < GameObjectPool::GetInstance().GetGameObjectCount(); i++)
	{
		GameObject* gameObject = gameObjects[i];

		// Find a camera
		if (gameObject->HasComponent<Camera>())
			m_mainCamera = gameObject;

		// Find renderables
		if (gameObject->HasComponent<MeshRenderer>())
			m_renderablePool.push_back(gameObject);

		// Find lights
		if (gameObject->HasComponent<Light>())
		{
			if (gameObject->GetComponent<Light>()->GetLightType() == Directional)
				m_directionalLightPool.push_back(gameObject);
			else if (gameObject->GetComponent<Light>()->GetLightType() == Point)
				m_pointLightPool.push_back(gameObject);
		}
	}
}

/*------------------------------------------------------------------------------
								[CREATE]
------------------------------------------------------------------------------*/
GameObject* Scene::CreateCamera()
{
	GameObject* camera = new GameObject();
	camera->SetName("Camera");
	camera->GetTransform()->SetPositionLocal(Vector3(0.0f, 1.0f, -5.0f));
	camera->AddComponent<Camera>();
	camera->AddComponent<Script>()->AddScript("Assets/Scripts/FirstPersonController.dss", 0);
	camera->AddComponent<LineRenderer>();
	camera->AddComponent<Skybox>();

	return camera;
}

GameObject* Scene::CreateDirectionalLight()
{
	GameObject* light = new GameObject();
	light->SetName("DirectionalLight");
	light->AddComponent<Light>();
	light->GetComponent<Transform>()->SetRotationLocal(Quaternion::FromEulerAngles(30.0f, 0.0, 0.0f));
	light->GetComponent<Light>()->SetLightType(Directional);
	light->GetComponent<Light>()->SetIntensity(8.0f);

	return light;
}
