/*
Copyright(c) 2016-2017 Panos Karabelas

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
#include <complex>
#include "../IO/Serializer.h"
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
#include "../Physics/Physics.h"
#include "../EventSystem/EventHandler.h"
#include "../Core/Context.h"
#include "Settings.h"
#include "../Resource/ResourceManager.h"
#include "Timer.h"
#include "../Components/Light.h"
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

		SUBSCRIBE_TO_EVENT(EVENT_UPDATE, this, Scene::Resolve);
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, this, Scene::Update);

		return true;
	}

	void Scene::Start()
	{
		for (const auto& gameObject : m_gameObjects)
			gameObject->Start();
	}

	void Scene::OnDisable()
	{
		for (const auto& gameObject : m_gameObjects)
			gameObject->OnDisable();
	}

	void Scene::Update()
	{
		for (const auto& gameObject : m_gameObjects)
			gameObject->Update();

		CalculateFPS();
	}

	void Scene::Clear()
	{
		m_gameObjects.clear();
		m_gameObjects.shrink_to_fit();

		m_renderables.clear();
		m_renderables.shrink_to_fit();

		m_lightsDirectional.clear();
		m_lightsDirectional.shrink_to_fit();

		m_lightsPoint.clear();
		m_lightsPoint.shrink_to_fit();

		// Clear the resource cache
		m_context->GetSubsystem<ResourceManager>()->Unload();

		// Clear/Reset subsystems that allocate some things
		m_context->GetSubsystem<Scripting>()->Reset();
		m_context->GetSubsystem<Physics>()->Reset();
		m_context->GetSubsystem<Renderer>()->Clear();
	}
	//=========================================================================================================

	//= I/O ===================================================================================================
	void Scene::SaveToFileAsync(const string& filePath)
	{
		m_context->GetSubsystem<Multithreading>()->AddTask(std::bind(&Scene::SaveToFile, this, filePath));
	}

	void Scene::LoadFromFileAsync(const string& filePath)
	{
		m_context->GetSubsystem<Multithreading>()->AddTask(std::bind(&Scene::LoadFromFile, this, filePath));
	}

	bool Scene::SaveToFile(const string& filePathIn)
	{
		// Add scene file extension to the filepath if it's missing
		string filePath = filePathIn;
		if (FileSystem::GetExtensionFromFilePath(filePath) != SCENE_EXTENSION)
		{
			filePath += SCENE_EXTENSION;
		}

		// Save any in-memory changes done to resources while running.
		m_context->GetSubsystem<ResourceManager>()->SaveResourceMetadata();

		if (!Serializer::StartWriting(filePath))
			return false;

		//= Save currently loaded resource paths =======================================================
		vector<string> resourcePaths = m_context->GetSubsystem<ResourceManager>()->GetResourceFilePaths();
		Serializer::WriteVectorSTR(resourcePaths);
		//==============================================================================================

		//= Save GameObjects ============================
		// Only save root GameObjects as they will also save their descendants
		vector<weakGameObj> rootGameObjects = GetRootGameObjects();

		// 1st - GameObject count
		Serializer::WriteInt((int)rootGameObjects.size());

		// 2nd - GameObject IDs
		for (const auto& root : rootGameObjects)
		{
			Serializer::WriteSTR(root.lock()->GetID());
		}

		// 3rd - GameObjects
		for (const auto& root : rootGameObjects)
		{
			root.lock()->Serialize();
		}
		//==============================================

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

		// Read all the resource file paths
		if (!Serializer::StartReading(filePath))
			return false;

		vector<string> resourcePaths = Serializer::ReadVectorSTR();
		Serializer::StopReading();

		// Load all all these resources
		auto resourceMng = m_context->GetSubsystem<ResourceManager>();
		for (const auto& resourcePath : resourcePaths)
		{
			if (FileSystem::IsEngineModelFile(resourcePath))
			{
				resourceMng->Load<Model>(resourcePath);
				continue;
			}

			if (FileSystem::IsEngineMaterialFile(resourcePath))
			{
				resourceMng->Load<Material>(resourcePath);
				continue;
			}

			if (FileSystem::IsSupportedImageFile(resourcePath))
			{
				resourceMng->Load<Texture>(resourcePath);
			}
		}


		if (!Serializer::StartReading(filePath))
			return false;

		// Read our way through the resource paths
		Serializer::ReadVectorSTR();

		//= Load GameObjects ============================	
		// 1st - GameObject count
		int rootGameObjectCount = Serializer::ReadInt();

		// 2nd - GameObject IDs
		for (int i = 0; i < rootGameObjectCount; i++)
		{

			auto gameObj = CreateGameObject().lock();
			gameObj->SetID(Serializer::ReadSTR());
		}

		// 3rd - GameObjects
		// It's important to loop with rootGameObjectCount
		// as the vector size will increase as we deserialize
		// GameObjects. This is because a GameObject will also
		// deserialize their descendants.
		for (int i = 0; i < rootGameObjectCount; i++)
		{
			m_gameObjects[i]->Deserialize(nullptr);
		}

		Serializer::StopReading();
		//==============================================

		Resolve();

		return true;
	}
	//===================================================================================================

	//= GAMEOBJECT HELPER FUNCTIONS  ====================================================================
	vector<weakGameObj> Scene::GetAllGameObjects()
	{
		vector<weakGameObj> allGameObj;
		for (const auto& gameObject : m_gameObjects)
		{
			allGameObj.push_back(gameObject);
		}

		return allGameObj;
	}

	vector<weakGameObj> Scene::GetRootGameObjects()
	{
		vector<weakGameObj> rootGameObjects;
		for (const auto& gameObj : m_gameObjects)
		{
			if (gameObj->GetTransform()->IsRoot())
			{
				rootGameObjects.push_back(gameObj);
			}
		}

		return rootGameObjects;
	}

	weakGameObj Scene::GetGameObjectRoot(weakGameObj gameObject)
	{
		return !gameObject.expired() ? gameObject.lock()->GetTransform()->GetRoot()->GetGameObject() : weakGameObj();
	}

	weakGameObj Scene::GetGameObjectByName(const string& name)
	{
		for (const auto& gameObject : m_gameObjects)
		{
			if (gameObject->GetName() == name)
			{
				return gameObject;
			}
		}

		return weakGameObj();
	}

	weakGameObj Scene::GetGameObjectByID(const string& ID)
	{
		for (const auto& gameObject : m_gameObjects)
		{
			if (gameObject->GetID() == ID)
			{
				return gameObject;
			}
		}

		return weakGameObj();
	}

	bool Scene::GameObjectExists(weakGameObj gameObject)
	{
		if (gameObject.expired())
			return false;

		return !GetGameObjectByID(gameObject.lock()->GetID()).expired() ? true : false;
	}

	// Removes a GameObject and all of it's children
	void Scene::RemoveGameObject(weakGameObj gameObject)
	{
		if (gameObject.expired())
			return;

		// remove any descendants
		vector<Transform*> descendants;
		gameObject.lock()->GetTransform()->GetDescendants(&descendants);
		for (const auto& descendant : descendants)
			RemoveSingleGameObject(descendant->GetGameObject());

		// remove this gameobject but keep it's parent
		Transform* parent = gameObject.lock()->GetTransform()->GetParent();
		RemoveSingleGameObject(gameObject);

		// if there is a parent, update it's children pool
		if (parent)
		{
			parent->ResolveChildrenRecursively();
		}
	}

	// Removes a GameObject but leaves the parent and the children as is
	void Scene::RemoveSingleGameObject(weakGameObj gameObject)
	{
		if (gameObject.expired())
			return;

		bool dirty = false;
		for (auto it = m_gameObjects.begin(); it < m_gameObjects.end();)
		{
			sharedGameObj temp = *it;
			if (temp->GetID() == gameObject.lock()->GetID())
			{
				it = m_gameObjects.erase(it);
				dirty = true;
				return;
			}
			++it;
		}

		if (dirty)
		{
			Resolve();
		}
	}
	//===================================================================================================

	//= SCENE RESOLUTION  ===============================================================================
	void Scene::Resolve()
	{
		m_renderables.clear();
		m_renderables.shrink_to_fit();

		m_lightsDirectional.clear();
		m_lightsDirectional.shrink_to_fit();

		m_lightsPoint.clear();
		m_lightsPoint.shrink_to_fit();

		for (const auto& gameObject : m_gameObjects)
		{
			// Find camera
			if (gameObject->HasComponent<Camera>())
				m_mainCamera = gameObject;

			// Find skybox
			if (gameObject->HasComponent<Skybox>())
				m_skybox = gameObject;

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
		sharedGameObj skybox = CreateGameObject().lock();
		skybox->SetName("Skybox");
		skybox->AddComponent<LineRenderer>();
		skybox->AddComponent<Skybox>();
		skybox->SetHierarchyVisibility(false);
		skybox->GetTransform()->SetParent(m_mainCamera.lock()->GetTransform());

		return skybox;
	}

	weak_ptr<GameObject> Scene::CreateCamera()
	{
		auto resourceMng = m_context->GetSubsystem<ResourceManager>();
		string scriptDirectory = resourceMng->GetResourceDirectory(Script_Resource);

		sharedGameObj camera = CreateGameObject().lock();
		camera->SetName("Camera");
		camera->AddComponent<Camera>();
		camera->GetTransform()->SetPositionLocal(Vector3(0.0f, 1.0f, -5.0f));
		camera->AddComponent<Script>()->AddScript(scriptDirectory + "MouseLook.as");
		camera->AddComponent<Script>()->AddScript(scriptDirectory + "FirstPersonController.as");

		return camera;
	}

	weak_ptr<GameObject> Scene::CreateDirectionalLight()
	{
		sharedGameObj light = CreateGameObject().lock();
		light->SetName("DirectionalLight");
		light->GetComponent<Transform>()->SetRotationLocal(Quaternion::FromEulerAngles(30.0f, 0.0, 0.0f));

		Light* lightComp = light->AddComponent<Light>();
		lightComp->SetLightType(Directional);
		lightComp->SetIntensity(4.0f);

		return light;
	}
	//======================================================================================================

	//= HELPER FUNCTIONS ===================================================================================
	void Scene::CalculateFPS()
	{
		// update counters
		m_frameCount++;
		m_timePassed += m_context->GetSubsystem<Timer>()->GetDeltaTime();

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
	weakGameObj Scene::CreateGameObject()
	{
		auto gameObj = make_shared<GameObject>(m_context);

		// First save the GameObject because the Transform (added below)
		// will call the scene to get the GameObject it's attached to
		m_gameObjects.push_back(gameObj);

		gameObj->Initialize(gameObj->AddComponent<Transform>());

		return gameObj;
	}
}
