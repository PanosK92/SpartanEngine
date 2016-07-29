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
#include "../Components/MeshFilter.h"
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

	//========================================
	m_texturePool->Serialize();
	m_materialPool->Serialize();
	m_meshPool->Serialize();
	GameObjectPool::GetInstance().Serialize();
	//========================================

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

	Serializer::StartReading(path);

	//==========================================
	m_texturePool->Deserialize();
	m_materialPool->Deserialize();
	m_meshPool->Deserialize();
	GameObjectPool::GetInstance().Deserialize();
	//==========================================

	Serializer::StopReading();

	m_isDirty = true;

	return true;
}

/*------------------------------------------------------------------------------
									[MISC]
------------------------------------------------------------------------------*/
void Scene::Clear()
{
	m_renderables.clear();
	m_renderables.shrink_to_fit();

	m_lightsDirectional.clear();
	m_lightsDirectional.shrink_to_fit();

	m_lightsPoint.clear();
	m_lightsPoint.shrink_to_fit();

	m_mainCamera = nullptr;

	m_texturePool->Clear();
	m_meshPool->Clear();
	m_materialPool->Clear();
	GameObjectPool::GetInstance().Clear();
	m_scriptEngine->Reset();
	m_physics->Reset();
	m_renderer->Clear();
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
}

/*------------------------------------------------------------------------------
							[SCENE ANALYSIS]
------------------------------------------------------------------------------*/
void Scene::AnalyzeGameObjects()
{
	m_renderables.clear();
	m_renderables.shrink_to_fit();

	m_lightsDirectional.clear();
	m_lightsDirectional.shrink_to_fit();

	m_lightsPoint.clear();
	m_lightsPoint.shrink_to_fit();

	// It's necessery to not forget to shit this to nullptr,
	// otherwise it can end up as a nice dangling pointer :-)
	m_mainCamera = nullptr;

	std::vector<GameObject*> gameObjects = GameObjectPool::GetInstance().GetAllGameObjects();
	for (int i = 0; i < GameObjectPool::GetInstance().GetGameObjectCount(); i++)
	{
		GameObject* gameobject = gameObjects[i];

		// Find a camera
		if (gameobject->HasComponent<Camera>())
			m_mainCamera = gameobject;

		// Find renderables
		if (gameobject->HasComponent<MeshRenderer>() && gameobject->HasComponent<MeshFilter>())
			m_renderables.push_back(gameobject);

		// Find lights
		if (gameobject->HasComponent<Light>())
		{
			if (gameobject->GetComponent<Light>()->GetLightType() == Directional)
				m_lightsDirectional.push_back(gameobject);
			else if (gameobject->GetComponent<Light>()->GetLightType() == Point)
				m_lightsPoint.push_back(gameobject);
		}
	}

	m_renderer->Update(m_renderables, m_lightsDirectional, m_lightsPoint);
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
	camera->AddComponent<Script>()->AddScript("Assets/Scripts/FirstPersonController.as", 0);
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
	light->GetComponent<Light>()->SetIntensity(4.0f);

	return light;
}
