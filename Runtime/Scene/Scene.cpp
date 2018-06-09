/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ===========================
#include "Scene.h"
#include "GameObject.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/Script.h"
#include "Components/LineRenderer.h"
#include "Components/Skybox.h"
#include "Components/Renderable.h"
#include "../Core/Timer.h"
#include "../Core/Context.h"
#include "../Core/Stopwatch.h"
#include "../Core/EventSystem.h"
#include "../Core/Backends_Imp.h"
#include "../Resource/ResourceManager.h"
#include "../Graphics/Mesh.h"
#include "../IO/FileStream.h"
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
#include "../Core/Engine.h"
#include "Components/AudioListener.h"
#include "../Profiling/Profiler.h"
#include "../Resource/ProgressReport.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Scene::Scene(Context* context) : Subsystem(context)
	{
		m_ambientLight = Vector3::Zero;
		m_fps = 0.0f;
		m_timePassed = 0.0f;
		m_frameCount = 0;

		SUBSCRIBE_TO_EVENT(EVENT_SCENE_RESOLVE, EVENT_HANDLER(Resolve));
		SUBSCRIBE_TO_EVENT(EVENT_UPDATE, EVENT_HANDLER(Update));
	}

	Scene::~Scene()
	{
		Clear();
	}

	bool Scene::Initialize()
	{
		m_mainCamera = CreateCamera();
		CreateSkybox();
		CreateDirectionalLight();
		Resolve();

		return true;
	}

	void Scene::Start()
	{
		for (const auto& gameObject : m_gameObjects)
		{
			gameObject->Start();
		}
	}

	void Scene::Stop()
	{
		for (const auto& gameObject : m_gameObjects)
		{
			gameObject->Stop();
		}
	}

	void Scene::Update()
	{	
		PROFILE_FUNCTION_BEGIN();

		//= DETECT TOGGLING TO GAME MODE =============================
		if (Engine::EngineMode_IsSet(Engine_Game) && m_isInEditorMode)
		{
			Start();
		}
		//============================================================
		//= DETECT TOGGLING TO EDITOR MODE ============================
		if (!Engine::EngineMode_IsSet(Engine_Game) && !m_isInEditorMode)
		{
			Stop();
		}
		//=============================================================
		m_isInEditorMode = !Engine::EngineMode_IsSet(Engine_Game);

		for (const auto& gameObject : m_gameObjects)
		{
			gameObject->Update();
		}

		ComputeFPS();

		PROFILE_FUNCTION_END();
	}

	void Scene::Clear()
	{
		m_gameObjects.clear();
		m_gameObjects.shrink_to_fit();

		m_renderables.clear();
		m_renderables.shrink_to_fit();

		FIRE_EVENT(EVENT_SCENE_CLEARED);
	}
	//=========================================================================================================

	//= I/O ===================================================================================================
	bool Scene::SaveToFile(const string& filePathIn)
	{
		ProgressReport::Get().Reset(g_progress_Scene);
		ProgressReport::Get().SetStatus(g_progress_Scene, "Saving scene...");
		Stopwatch timer;
	
		// Add scene file extension to the filepath if it's missing
		string filePath = filePathIn;
		if (FileSystem::GetExtensionFromFilePath(filePath) != SCENE_EXTENSION)
		{
			filePath += SCENE_EXTENSION;
		}

		// Save any in-memory changes done to resources while running.
		m_context->GetSubsystem<ResourceManager>()->SaveResourcesToFiles();

		// Create a prefab file
		auto file = make_unique<FileStream>(filePath, FileStreamMode_Write);
		if (!file->IsOpen())
			return false;

		// Save currently loaded resource paths
		vector<string> filePaths;
		m_context->GetSubsystem<ResourceManager>()->GetResourceFilePaths(filePaths);
		file->Write(filePaths);

		//= Save GameObjects ============================
		// Only save root GameObjects as they will also save their descendants
		vector<weak_ptr<GameObject>> rootGameObjects = GetRootGameObjects();

		// 1st - GameObject count
		auto rootGameObjectCount = (int)rootGameObjects.size();
		file->Write(rootGameObjectCount);

		// 2nd - GameObject IDs
		for (const auto& root : rootGameObjects)
		{
			file->Write(root.lock()->GetID());
		}

		// 3rd - GameObjects
		for (const auto& root : rootGameObjects)
		{
			root.lock()->Serialize(file.get());
		}
		//==============================================

		LOG_INFO("Scene: Saving took " + to_string((int)timer.GetElapsedTimeMs()) + " ms");
		FIRE_EVENT(EVENT_SCENE_SAVED);

		ProgressReport::Get().SetIsLoading(g_progress_Scene, false);

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
		ProgressReport::Get().Reset(g_progress_Scene);
		ProgressReport::Get().SetStatus(g_progress_Scene, "Loading scene...");

		// Read all the resource file paths
		auto file = make_unique<FileStream>(filePath, FileStreamMode_Read);
		if (!file->IsOpen())
			return false;

		Stopwatch timer;

		vector<string> resourcePaths;
		file->Read(&resourcePaths);

		ProgressReport::Get().SetJobCount(g_progress_Scene, (int)resourcePaths.size());

		// Load all the resources
		auto resourceMng = m_context->GetSubsystem<ResourceManager>();
		for (const auto& resourcePath : resourcePaths)
		{
			if (FileSystem::IsEngineMeshFile(resourcePath))
			{
				resourceMng->Load<Mesh>(resourcePath);
			}

			if (FileSystem::IsEngineModelFile(resourcePath))
			{
				resourceMng->Load<Model>(resourcePath);
			}

			if (FileSystem::IsEngineMaterialFile(resourcePath))
			{
				resourceMng->Load<Material>(resourcePath);
			}

			if (FileSystem::IsEngineTextureFile(resourcePath))
			{
				resourceMng->Load<Texture>(resourcePath);
			}

			ProgressReport::Get().JobDone(g_progress_Scene);
		}

		//= Load GameObjects ============================	
		// 1st - Root GameObject count
		int rootGameObjectCount = file->ReadInt();

		// 2nd - Root GameObject IDs
		for (int i = 0; i < rootGameObjectCount; i++)
		{
			auto gameObj = GameObject_CreateAdd().lock();
			gameObj->SetID(file->ReadInt());
		}

		// 3rd - GameObjects
		// It's important to loop with rootGameObjectCount
		// as the vector size will increase as we deserialize
		// GameObjects. This is because a GameObject will also
		// deserialize their descendants.
		for (int i = 0; i < rootGameObjectCount; i++)
		{
			m_gameObjects[i]->Deserialize(file.get(), nullptr);
		}
		//==============================================

		Resolve();
		ProgressReport::Get().SetIsLoading(g_progress_Scene, false);
		LOG_INFO("Scene: Loading took " + to_string((int)timer.GetElapsedTimeMs()) + " ms");	
		FIRE_EVENT(EVENT_SCENE_LOADED);

		return true;
	}
	//===================================================================================================

	//= GAMEOBJECT HELPER FUNCTIONS  ====================================================================
	weak_ptr<GameObject> Scene::GameObject_CreateAdd()
	{
		auto gameObj = make_shared<GameObject>(m_context);

		// First keep a local reference to this GameObject because 
		// as the Transform (added below) will call us back to get a reference to it
		m_gameObjects.emplace_back(gameObj);

		gameObj->Initialize(gameObj->AddComponent<Transform>().lock().get());

		return gameObj;
	}

	void Scene::GameObject_Add(shared_ptr<GameObject> gameObject)
	{
		if (!gameObject)
			return;

		m_gameObjects.emplace_back(gameObject);
	}

	bool Scene::GameObject_Exists(const weak_ptr<GameObject>& gameObject)
	{
		if (gameObject.expired())
			return false;

		return !GetGameObjectByID(gameObject.lock()->GetID()).expired();
	}

	// Removes a GameObject and all of it's children
	void Scene::GameObject_Remove(const weak_ptr<GameObject>& gameObject)
	{
		GameObject* gameObjPtr = gameObject.lock().get();
		if (!gameObjPtr)
			return;

		// remove any descendants
		vector<Transform*> children = gameObjPtr->GetTransform_PtrRaw()->GetChildren();
		for (const auto& child : children)
		{
			GameObject_Remove(child->GetGameObject_PtrWeak());
		}

		// Keep a reference to it's parent (in case it has one)
		Transform* parent = gameObjPtr->GetTransform_PtrRaw()->GetParent();

		// Remove this GameObject
		for (auto it = m_gameObjects.begin(); it < m_gameObjects.end();)
		{
			shared_ptr<GameObject> temp = *it;
			if (temp->GetID() == gameObjPtr->GetID())
			{
				it = m_gameObjects.erase(it);
				break;
			}
			++it;
		}

		// If there was a parent, update it
		if (parent)
		{
			parent->ResolveChildrenRecursively();
		}

		Resolve();
	}

	vector<weak_ptr<GameObject>> Scene::GetRootGameObjects()
	{
		vector<weak_ptr<GameObject>> rootGameObjects;
		for (const auto& gameObj : m_gameObjects)
		{
			if (gameObj->GetTransform_PtrRaw()->IsRoot())
			{
				rootGameObjects.emplace_back(gameObj);
			}
		}

		return rootGameObjects;
	}

	weak_ptr<GameObject> Scene::GetGameObjectRoot(weak_ptr<GameObject> gameObject)
	{
		if (gameObject.expired())
			return weak_ptr<GameObject>();

		return gameObject.lock()->GetTransform_PtrRaw()->GetRoot()->GetGameObject_PtrWeak();
	}

	weak_ptr<GameObject> Scene::GetGameObjectByName(const string& name)
	{
		for (const auto& gameObject : m_gameObjects)
		{
			if (gameObject->GetName() == name)
			{
				return gameObject;
			}
		}

		return weak_ptr<GameObject>();
	}

	weak_ptr<GameObject> Scene::GetGameObjectByID(unsigned int ID)
	{
		for (const auto& gameObject : m_gameObjects)
		{
			if (gameObject->GetID() == ID)
			{
				return gameObject;
			}
		}

		return weak_ptr<GameObject>();
	}
	//===================================================================================================

	//= SCENE RESOLUTION  ===============================================================================
	void Scene::Resolve()
	{
		PROFILE_FUNCTION_BEGIN();

		m_renderables.clear();
		m_renderables.shrink_to_fit();

		for (const auto& gameObject : m_gameObjects)
		{
			static bool hasCamera = false;
			static bool hasSkybox = false;

			// Find camera
			if (gameObject->HasComponent<Camera>())
			{
				m_mainCamera = gameObject;
				hasCamera = true;
			}

			// Find skybox
			if (gameObject->HasComponent<Skybox>())
			{
				m_skybox = gameObject;
				hasSkybox = true;
			}

			// Find renderables
			if (gameObject->HasComponent<Renderable>() || hasCamera || hasSkybox || gameObject->HasComponent<Light>())
			{
				m_renderables.push_back(gameObject);
			}
		}

		PROFILE_FUNCTION_END();
		FIRE_EVENT_DATA(EVENT_SCENE_RESOLVED, m_renderables);
	}
	//===================================================================================================

	//= TEMPORARY EXPERIMENTS  ==========================================================================
	void Scene::SetAmbientLight(float x, float y, float z)
	{
		m_ambientLight = Vector3(x, y, z);
	}

	Vector3 Scene::GetAmbientLight()
	{
		return m_ambientLight;
	}
	//======================================================================================================

	//= COMMON GAMEOBJECT CREATION =========================================================================
	weak_ptr<GameObject> Scene::CreateSkybox()
	{
		shared_ptr<GameObject> skybox = GameObject_CreateAdd().lock();
		skybox->SetName("Skybox");
		skybox->SetHierarchyVisibility(false);
		skybox->AddComponent<LineRenderer>();
		skybox->AddComponent<Skybox>();	
		skybox->GetTransform_PtrRaw()->SetParent(m_mainCamera.lock()->GetTransform_PtrRaw());

		return skybox;
	}

	weak_ptr<GameObject> Scene::CreateCamera()
	{
		auto resourceMng = m_context->GetSubsystem<ResourceManager>();
		string scriptDirectory = resourceMng->GetStandardResourceDirectory(Resource_Script);

		shared_ptr<GameObject> camera = GameObject_CreateAdd().lock();
		camera->SetName("Camera");
		camera->AddComponent<Camera>();
		camera->AddComponent<AudioListener>();
		camera->AddComponent<Script>().lock()->SetScript(scriptDirectory + "MouseLook.as");
		camera->AddComponent<Script>().lock()->SetScript(scriptDirectory + "FirstPersonController.as");
		camera->GetTransform_PtrRaw()->SetPositionLocal(Vector3(0.0f, 1.0f, -5.0f));

		return camera;
	}

	weak_ptr<GameObject> Scene::CreateDirectionalLight()
	{
		shared_ptr<GameObject> light = GameObject_CreateAdd().lock();
		light->SetName("DirectionalLight");
		light->GetTransform_PtrRaw()->SetRotationLocal(Quaternion::FromEulerAngles(30.0f, 0.0, 0.0f));
		light->GetTransform_PtrRaw()->SetPosition(Vector3(0.0f, 10.0f, 0.0f));

		Light* lightComp = light->AddComponent<Light>().lock().get();
		lightComp->SetLightType(LightType_Directional);
		lightComp->SetIntensity(2.0f);

		return light;
	}
	//======================================================================================================

	//= HELPER FUNCTIONS ===================================================================================
	void Scene::ComputeFPS()
	{
		// update counters
		m_frameCount++;
		m_timePassed += m_context->GetSubsystem<Timer>()->GetDeltaTimeMs();

		if (m_timePassed >= 1000)
		{
			// calculate fps
			m_fps = (float)m_frameCount / (m_timePassed / 1000.0f);

			// reset counters
			m_frameCount = 0;
			m_timePassed = 0;
		}
	}
	//======================================================================================================
}
