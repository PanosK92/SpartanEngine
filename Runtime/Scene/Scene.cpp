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
#include "Actor.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/Script.h"
#include "Components/LineRenderer.h"
#include "Components/Skybox.h"
#include "Components/AudioListener.h"
#include "../Core/Engine.h"
#include "../Core/Timer.h"
#include "../Core/Stopwatch.h"
#include "../Resource/ResourceManager.h"
#include "../Resource/ProgressReport.h"
#include "../IO/FileStream.h"
#include "../Profiling/Profiler.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Scene::Scene(Context* context) : Subsystem(context)
	{
		m_ambientLight	= Vector3::Zero;

		SUBSCRIBE_TO_EVENT(EVENT_SCENE_RESOLVE, EVENT_HANDLER(Resolve));
		SUBSCRIBE_TO_EVENT(EVENT_TICK, EVENT_HANDLER(Update));
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
		for (const auto& actor : m_actors)
		{
			actor->Start();
		}
	}

	void Scene::Stop()
	{
		for (const auto& actor : m_actors)
		{
			actor->Stop();
		}
	}

	void Scene::Update()
	{	
		TIME_BLOCK_START_CPU();

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

		for (const auto& actor : m_actors)
		{
			actor->Tick();
		}

		TIME_BLOCK_END_CPU();
	}

	void Scene::Clear()
	{
		m_actors.clear();
		m_actors.shrink_to_fit();

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
		if (FileSystem::GetExtensionFromFilePath(filePath) != EXTENSION_SCENE)
		{
			filePath += EXTENSION_SCENE;
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

		//= Save actors ============================
		// Only save root actors as they will also save their descendants
		vector<weak_ptr<Actor>> rootactors = GetRootActors();

		// 1st - actor count
		auto rootactorCount = (int)rootactors.size();
		file->Write(rootactorCount);

		// 2nd - actor IDs
		for (const auto& root : rootactors)
		{
			file->Write(root.lock()->GetID());
		}

		// 3rd - actors
		for (const auto& root : rootactors)
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
				resourceMng->Load<RHI_Texture>(resourcePath);
			}

			ProgressReport::Get().JobDone(g_progress_Scene);
		}

		//= Load actors ============================	
		// 1st - Root actor count
		int rootactorCount = file->ReadInt();

		// 2nd - Root actor IDs
		for (int i = 0; i < rootactorCount; i++)
		{
			auto actor = Actor_CreateAdd().lock();
			actor->SetID(file->ReadInt());
		}

		// 3rd - actors
		// It's important to loop with rootactorCount
		// as the vector size will increase as we deserialize
		// actors. This is because a actor will also
		// deserialize their descendants.
		for (int i = 0; i < rootactorCount; i++)
		{
			m_actors[i]->Deserialize(file.get(), nullptr);
		}
		//==============================================

		Resolve();
		ProgressReport::Get().SetIsLoading(g_progress_Scene, false);
		LOG_INFO("Scene: Loading took " + to_string((int)timer.GetElapsedTimeMs()) + " ms");	
		FIRE_EVENT(EVENT_SCENE_LOADED);

		return true;
	}
	//===================================================================================================

	//= actor HELPER FUNCTIONS  ====================================================================
	weak_ptr<Actor> Scene::Actor_CreateAdd()
	{
		auto actor = make_shared<Actor>(m_context);

		// First keep a local reference to this actor because 
		// as the Transform (added below) will call us back to get a reference to it
		m_actors.emplace_back(actor);

		actor->Initialize(actor->AddComponent<Transform>().lock().get());

		return actor;
	}

	void Scene::Actor_Add(shared_ptr<Actor> actor)
	{
		if (!actor)
			return;

		m_actors.emplace_back(actor);
	}

	bool Scene::Actor_Exists(const weak_ptr<Actor>& actor)
	{
		if (actor.expired())
			return false;

		return !GetActorByID(actor.lock()->GetID()).expired();
	}

	// Removes an actor and all of it's children
	void Scene::Actor_Remove(const weak_ptr<Actor>& actor)
	{
		Actor* actorPtr = actor.lock().get();
		if (!actorPtr)
			return;

		// remove any descendants
		vector<Transform*> children = actorPtr->GetTransform_PtrRaw()->GetChildren();
		for (const auto& child : children)
		{
			Actor_Remove(child->GetActor_PtrWeak());
		}

		// Keep a reference to it's parent (in case it has one)
		Transform* parent = actorPtr->GetTransform_PtrRaw()->GetParent();

		// Remove this actor
		for (auto it = m_actors.begin(); it < m_actors.end();)
		{
			shared_ptr<Actor> temp = *it;
			if (temp->GetID() == actorPtr->GetID())
			{
				it = m_actors.erase(it);
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

	vector<weak_ptr<Actor>> Scene::GetRootActors()
	{
		vector<weak_ptr<Actor>> rootactors;
		for (const auto& actor : m_actors)
		{
			if (actor->GetTransform_PtrRaw()->IsRoot())
			{
				rootactors.emplace_back(actor);
			}
		}

		return rootactors;
	}

	weak_ptr<Actor> Scene::GetActorRoot(weak_ptr<Actor> actor)
	{
		if (actor.expired())
			return weak_ptr<Actor>();

		return actor.lock()->GetTransform_PtrRaw()->GetRoot()->GetActor_PtrWeak();
	}

	weak_ptr<Actor> Scene::GetActorByName(const string& name)
	{
		for (const auto& actor : m_actors)
		{
			if (actor->GetName() == name)
			{
				return actor;
			}
		}

		return weak_ptr<Actor>();
	}

	weak_ptr<Actor> Scene::GetActorByID(unsigned int ID)
	{
		for (const auto& actor : m_actors)
		{
			if (actor->GetID() == ID)
			{
				return actor;
			}
		}

		return weak_ptr<Actor>();
	}
	//===================================================================================================

	//= SCENE RESOLUTION  ===============================================================================
	void Scene::Resolve()
	{
		TIME_BLOCK_START_CPU();

		m_renderables.clear();
		m_renderables.shrink_to_fit();

		for (const auto& actor : m_actors)
		{
			static bool hasCamera = false;
			static bool hasSkybox = false;

			// Find camera
			if (actor->HasComponent<Camera>())
			{
				m_mainCamera = actor;
				hasCamera = true;
			}

			// Find skybox
			if (actor->HasComponent<Skybox>())
			{
				m_skybox = actor;
				hasSkybox = true;
			}

			// Find renderables
			if (actor->HasComponent<Renderable>() || hasCamera || hasSkybox || actor->HasComponent<Light>())
			{
				m_renderables.push_back(actor);
			}
		}

		TIME_BLOCK_END_CPU();
		FIRE_EVENT_DATA(EVENT_SCENE_RESOLVED, m_renderables);
	}
	//===================================================================================================

	//= TEMPORARY EXPERIMENTS  ===========================
	void Scene::SetAmbientLight(float x, float y, float z)
	{
		m_ambientLight = Vector3(x, y, z);
	}

	Vector3 Scene::GetAmbientLight()
	{
		return m_ambientLight;
	}
	//====================================================

	//= COMMON ACTOR CREATION ========================================================================
	weak_ptr<Actor> Scene::CreateSkybox()
	{
		shared_ptr<Actor> skybox = Actor_CreateAdd().lock();
		skybox->SetName("Skybox");
		skybox->SetHierarchyVisibility(false);
		skybox->AddComponent<LineRenderer>();
		skybox->AddComponent<Skybox>();	
		skybox->GetTransform_PtrRaw()->SetParent(m_mainCamera.lock()->GetTransform_PtrRaw());

		return skybox;
	}

	weak_ptr<Actor> Scene::CreateCamera()
	{
		auto resourceMng = m_context->GetSubsystem<ResourceManager>();
		string scriptDirectory = resourceMng->GetStandardResourceDirectory(Resource_Script);

		shared_ptr<Actor> camera = Actor_CreateAdd().lock();
		camera->SetName("Camera");
		camera->AddComponent<Camera>();
		camera->AddComponent<AudioListener>();
		camera->AddComponent<Script>().lock()->SetScript(scriptDirectory + "MouseLook.as");
		camera->AddComponent<Script>().lock()->SetScript(scriptDirectory + "FirstPersonController.as");
		camera->GetTransform_PtrRaw()->SetPositionLocal(Vector3(0.0f, 1.0f, -5.0f));

		return camera;
	}

	weak_ptr<Actor> Scene::CreateDirectionalLight()
	{
		shared_ptr<Actor> light = Actor_CreateAdd().lock();
		light->SetName("DirectionalLight");
		light->GetTransform_PtrRaw()->SetRotationLocal(Quaternion::FromEulerAngles(30.0f, 0.0, 0.0f));
		light->GetTransform_PtrRaw()->SetPosition(Vector3(0.0f, 10.0f, 0.0f));

		Light* lightComp = light->AddComponent<Light>().lock().get();
		lightComp->SetLightType(LightType_Directional);
		lightComp->SetIntensity(3.0f);

		return light;
	}
	//================================================================================================
}
