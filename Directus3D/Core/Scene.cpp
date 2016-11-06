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
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
#include "../Graphics//Renderer.h"
#include "../Components/Transform.h"
#include "../Components/MeshRenderer.h"
#include "../Components/Camera.h"
#include "../Components/LineRenderer.h"
#include "../Components/Skybox.h"
#include "../Components/Script.h"
#include "../Components/MeshFilter.h"
#include "../Physics/PhysicsWorld.h"
#include "../Signals/Signaling.h"
#include "../Core/Context.h"
#include "../Pools/ShaderPool.h"
#include "../Pools/TexturePool.h"
//=====================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

Scene::Scene(Context* context) : Object(context)
{
	m_ambientLight = Vector3::Zero;
	m_mainCamera = nullptr;
}

Scene::~Scene()
{
	Clear();
}

void Scene::Initialize()
{
	GameObjectPool::GetInstance().Initialize(g_context);

	m_mainCamera = CreateCamera();
	CreateSkybox();
	CreateDirectionalLight();

	Resolve();
}

//= I/O ===========================================================================
void Scene::SaveToFileAsync(const string& filePath)
{
	g_context->GetSubsystem<ThreadPool>()->AddTask(std::bind(&Scene::SaveToFile, this, filePath));
}

void Scene::LoadFromFileAsync(const string& filePath)
{
	g_context->GetSubsystem<ThreadPool>()->AddTask(std::bind(&Scene::LoadFromFile, this, filePath));
}

bool Scene::SaveToFile(const string& filePathIn)
{
	string filePath = filePathIn;

	// Add scene file extension to the filepath if it's missing
	if (FileSystem::GetExtensionFromPath(filePath) != SCENE_EXTENSION)
		filePath += SCENE_EXTENSION;

	// Any in-memory changes done to resources while running, must be saved...
	g_context->GetSubsystem<TexturePool>()->SaveTextureMetadata();
	g_context->GetSubsystem<MaterialPool>()->SaveMaterialMetadata();

	Serializer::StartWriting(filePath);

	// Gather all the paths of any resource files used by the scene
	vector<string> texturePaths = g_context->GetSubsystem<TexturePool>()->GetAllTextureFilePaths();
	vector<string> materialPaths = g_context->GetSubsystem<MaterialPool>()->GetAllMaterialFilePaths();
	vector<string> meshPaths = g_context->GetSubsystem<MeshPool>()->GetAllMeshFilePaths();

	// Save all the paths
	Serializer::WriteVectorSTR(texturePaths);
	Serializer::WriteVectorSTR(materialPaths);
	Serializer::WriteVectorSTR(meshPaths);

	// Save the GameObjects
	GameObjectPool::GetInstance().Serialize();

	Serializer::StopWriting();

	return true;
}

bool Scene::LoadFromFile(const string& filePath)
{
	if (!FileSystem::FileExists(filePath))
	{
		LOG_ERROR(filePath + " was not found.");
		return false;
	}
	Clear();

	// Read all the paths of any resource files used by the scene
	//===========================================================
	Serializer::StartReading(filePath);

	// Gather all the paths of any resource files used by the scene
	vector<string> texturePaths = Serializer::ReadVectorSTR();
	vector<string> materialPaths = Serializer::ReadVectorSTR();
	vector<string> meshPaths = Serializer::ReadVectorSTR();

	Serializer::StopReading();
	//===========================================================

	// Load all the used resources into memory
	g_context->GetSubsystem<TexturePool>()->Add(texturePaths);
	g_context->GetSubsystem<MaterialPool>()->Add(materialPaths);
	g_context->GetSubsystem<MeshPool>()->Add(meshPaths);
	
	// Load all the GameObjects present in the scene
	//==============================================
	Serializer::StartReading(filePath);

	// We read our way to the point where GameObject data starts.
	// There might be a more elegant solution here, but the 
	// brute force approach should do too.
	Serializer::ReadVectorSTR();
	Serializer::ReadVectorSTR();
	Serializer::ReadVectorSTR();

	// Load the GameObjects
	GameObjectPool::GetInstance().Deserialize();	

	Serializer::StopReading();
	//==============================================

	Resolve();

	return true;
}
//======================================================================

//=====
// MISC
//=====
void Scene::Clear()
{
	m_renderables.clear();
	m_renderables.shrink_to_fit();

	m_lightsDirectional.clear();
	m_lightsDirectional.shrink_to_fit();

	m_lightsPoint.clear();
	m_lightsPoint.shrink_to_fit();

	m_mainCamera = nullptr;

	//= Clear all the pools ==================
	g_context->GetSubsystem<ShaderPool>()->Clear();
	g_context->GetSubsystem<TexturePool>()->Clear();
	g_context->GetSubsystem<MaterialPool>()->Clear();
	g_context->GetSubsystem<MeshPool>()->Clear();			
	GameObjectPool::GetInstance().Clear();
	//========================================

	g_context->GetSubsystem<ScriptEngine>()->Reset();
	g_context->GetSubsystem<PhysicsWorld>()->Reset();
	g_context->GetSubsystem<Renderer>()->Clear();

	CreateSkybox();
}

GameObject* Scene::GetSkybox()
{
	for (GameObject* gameObject : GameObjectPool::GetInstance().GetAllGameObjects())
		if (gameObject->HasComponent<Skybox>())
			return gameObject;

	return nullptr;
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

void Scene::Resolve()
{
	GameObjectPool::GetInstance().Update();

	m_renderables.clear();
	m_renderables.shrink_to_fit();

	m_lightsDirectional.clear();
	m_lightsDirectional.shrink_to_fit();

	m_lightsPoint.clear();
	m_lightsPoint.shrink_to_fit();

	// It's necessery not to forget to set this to nullptr,
	// otherwise it can end up as a nice dangling pointer :-)
	m_mainCamera = nullptr;

	for (auto gameObject : GameObjectPool::GetInstance().GetAllGameObjects())
	{
		// Find a camera
		if (gameObject->HasComponent<Camera>())
			m_mainCamera = gameObject;

		// Find renderables
		if (gameObject->HasComponent<MeshRenderer>() && gameObject->HasComponent<MeshFilter>())
			m_renderables.push_back(gameObject);

		// Find lights
		if (gameObject->HasComponent<Light>())
		{
			if (gameObject->GetComponent<Light>()->GetLightType() == Directional)
				m_lightsDirectional.push_back(gameObject);
			else if (gameObject->GetComponent<Light>()->GetLightType() == Point)
				m_lightsPoint.push_back(gameObject);
		}
	}

	g_context->GetSubsystem<Renderer>()->Update(m_renderables, m_lightsDirectional, m_lightsPoint);
}

//====================
// GAMEOBJECT CREATION
//====================
GameObject* Scene::CreateSkybox()
{
	GameObject* skybox = new GameObject();
	skybox->SetName("Skybox");
	skybox->AddComponent<LineRenderer>();
	skybox->AddComponent<Skybox>();

	return skybox;
}

GameObject* Scene::CreateCamera()
{
	GameObject* camera = new GameObject();
	camera->SetName("Camera");
	camera->AddComponent<Camera>();
	camera->GetTransform()->SetPositionLocal(Vector3(0.0f, 1.0f, -5.0f));
	
	camera->AddComponent<Script>()->AddScript("Assets/Scripts/MouseLook.as");
	camera->AddComponent<Script>()->AddScript("Assets/Scripts/FirstPersonController.as");

	return camera;
}

GameObject* Scene::CreateDirectionalLight()
{
	GameObject* light = new GameObject();
	light->SetName("DirectionalLight");
	light->GetComponent<Transform>()->SetRotationLocal(Quaternion::FromEulerAngles(30.0f, 0.0, 0.0f));

	Light* lightComp = light->AddComponent<Light>();
	lightComp->SetLightType(Directional);
	lightComp->SetIntensity(4.0f);

	return light;
}