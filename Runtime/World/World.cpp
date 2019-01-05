/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "World.h"
#include "Actor.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/Script.h"
#include "Components/Skybox.h"
#include "Components/AudioListener.h"
#include "Components/Renderable.h"
#include "../Core/Engine.h"
#include "../Core/Stopwatch.h"
#include "../Resource/ResourceManager.h"
#include "../Resource/ProgressReport.h"
#include "../IO/FileStream.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	namespace _World
	{
		shared_ptr<Actor> emptyActor;
	}

	World::World(Context* context) : Subsystem(context)
	{
		m_state = Ticking;
		SUBSCRIBE_TO_EVENT(EVENT_WORLD_RESOLVE, [this](Variant) { m_isDirty = true; });
		SUBSCRIBE_TO_EVENT(EVENT_TICK, EVENT_HANDLER(Tick));
		SUBSCRIBE_TO_EVENT(EVENT_WORLD_STOP, [this](Variant)	{ m_state = Idle; });
		SUBSCRIBE_TO_EVENT(EVENT_WORLD_START, [this](Variant)	{ m_state = Ticking; });
	}

	World::~World()
	{
		Unload();
	}

	bool World::Initialize()
	{
		m_isDirty = true;
		CreateCamera();
		CreateSkybox();
		CreateDirectionalLight();

		return true;
	}

	void World::Tick()
	{	
		if (m_state == Request_Loading)
		{
			m_state = Loading;
			return;
		}

		if (m_state != Ticking)
			return;

		TIME_BLOCK_START_CPU();
		
		// Detect game toggling
		bool started		= Engine::EngineMode_IsSet(Engine_Game) && m_wasInEditorMode;
		bool stopped		= !Engine::EngineMode_IsSet(Engine_Game) && !m_wasInEditorMode;
		m_wasInEditorMode	= !Engine::EngineMode_IsSet(Engine_Game);

		// ACTOR START
		if (started)
		{			
			for (const auto& actor : m_actorsPrimary)
			{
				actor->Start();
			}
		}
		// ACTOR STOP
		if (stopped)
		{		
			for (const auto& actor : m_actorsPrimary)
			{
				actor->Stop();
			}
		}
		// ACTOR TICK
		for (const auto& actor : m_actorsPrimary)
		{
			actor->Tick();
		}

		TIME_BLOCK_END_CPU();

		if (m_isDirty)
		{
			m_actorsSecondry = m_actorsPrimary;
			// Submit to the Renderer
			FIRE_EVENT_DATA(EVENT_WORLD_SUBMIT, m_actorsSecondry);
			m_isDirty = false;
		}
	}

	void World::Unload()
	{
		FIRE_EVENT(EVENT_WORLD_UNLOAD);
		m_actorsPrimary.clear();
		m_actorsPrimary.shrink_to_fit();
	}
	//=========================================================================================================

	//= I/O ===================================================================================================
	bool World::SaveToFile(const string& filePathIn)
	{
		ProgressReport::Get().Reset(g_progress_Scene);
		ProgressReport::Get().SetIsLoading(g_progress_Scene, true);
		ProgressReport::Get().SetStatus(g_progress_Scene, "Saving scene...");
		Stopwatch timer;
	
		// Add scene file extension to the filepath if it's missing
		string filePath = filePathIn;
		if (FileSystem::GetExtensionFromFilePath(filePath) != EXTENSION_WORLD)
		{
			filePath += EXTENSION_WORLD;
		}

		// Save any in-memory changes done to resources while running.
		m_context->GetSubsystem<ResourceManager>()->SaveResourcesToFiles();

		// Create a prefab file
		auto file = make_unique<FileStream>(filePath, FileStreamMode_Write);
		if (!file->IsOpen())
		{
			return false;
		}

		// Save currently loaded resource paths
		vector<string> filePaths;
		m_context->GetSubsystem<ResourceManager>()->GetResourceFilePaths(filePaths);
		file->Write(filePaths);

		//= Save actors ============================
		// Only save root actors as they will also save their descendants
		vector<shared_ptr<Actor>> rootActors = Actors_GetRoots();

		// 1st - actor count
		auto rootActorCount = (int)rootActors.size();
		file->Write(rootActorCount);

		// 2nd - actor IDs
		for (const auto& root : rootActors)
		{
			file->Write(root->GetID());
		}

		// 3rd - actors
		for (const auto& root : rootActors)
		{
			root->Serialize(file.get());
		}
		//==============================================

		ProgressReport::Get().SetIsLoading(g_progress_Scene, false);
		LOG_INFO("Scene: Saving took " + to_string((int)timer.GetElapsedTimeMs()) + " ms");	
		FIRE_EVENT(EVENT_WORLD_SAVED);

		return true;
	}

	bool World::LoadFromFile(const string& filePath)
	{
		if (!FileSystem::FileExists(filePath))
		{
			LOG_ERROR(filePath + " was not found.");
			return false;
		}

		// Thread safety: Wait for scene and the renderer to stop the actors (could do double buffering in the future)
		while (m_state != Loading || Renderer::IsRendering()) { m_state = Request_Loading; this_thread::sleep_for(chrono::milliseconds(16)); }

		ProgressReport::Get().Reset(g_progress_Scene);
		ProgressReport::Get().SetIsLoading(g_progress_Scene, true);
		ProgressReport::Get().SetStatus(g_progress_Scene, "Loading scene...");

		Unload();

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

			ProgressReport::Get().IncrementJobsDone(g_progress_Scene);
		}

		//= Load actors ============================	
		// 1st - Root actor count
		int rootactorCount = file->ReadInt();

		// 2nd - Root actor IDs
		for (int i = 0; i < rootactorCount; i++)
		{
			auto& actor = Actor_Create();
			actor->SetID(file->ReadInt());
		}

		// 3rd - actors
		// It's important to loop with rootactorCount
		// as the vector size will increase as we deserialize
		// actors. This is because a actor will also
		// deserialize their descendants.
		for (int i = 0; i < rootactorCount; i++)
		{
			m_actorsPrimary[i]->Deserialize(file.get(), nullptr);
		}
		//==============================================

		m_isDirty	= true;
		m_state		= Ticking;
		ProgressReport::Get().SetIsLoading(g_progress_Scene, false);	
		LOG_INFO("Scene: Loading took " + to_string((int)timer.GetElapsedTimeMs()) + " ms");	

		FIRE_EVENT(EVENT_WORLD_LOADED);
		return true;
	}
	//===================================================================================================

	//= Actor HELPER FUNCTIONS  ====================================================================
	shared_ptr<Actor>& World::Actor_Create()
	{
		auto actor = make_shared<Actor>(m_context);
		actor->Initialize(actor->AddComponent<Transform>().get());
		return m_actorsPrimary.emplace_back(actor);
	}

	shared_ptr<Actor>& World::Actor_Add(const shared_ptr<Actor>& actor)
	{
		if (!actor)
			return m_actorEmpty;

		return m_actorsPrimary.emplace_back(actor);
	}

	bool World::Actor_Exists(const weak_ptr<Actor>& actor)
	{
		if (actor.expired())
			return false;

		return Actor_GetByID(actor.lock()->GetID()) != nullptr;
	}

	// Removes an actor and all of it's children
	void World::Actor_Remove(const weak_ptr<Actor>& actor)
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
		for (auto it = m_actorsPrimary.begin(); it < m_actorsPrimary.end();)
		{
			shared_ptr<Actor> temp = *it;
			if (temp->GetID() == actorPtr->GetID())
			{
				it = m_actorsPrimary.erase(it);
				break;
			}
			++it;
		}

		// If there was a parent, update it
		if (parent)
		{
			parent->AcquireChildren();
		}

		m_isDirty = true;
	}

	vector<shared_ptr<Actor>> World::Actors_GetRoots()
	{
		vector<shared_ptr<Actor>> rootActors;
		for (const auto& actor : m_actorsPrimary)
		{
			if (actor->GetTransform_PtrRaw()->IsRoot())
			{
				rootActors.emplace_back(actor);
			}
		}

		return rootActors;
	}

	const shared_ptr<Actor>& World::Actor_GetByName(const string& name)
	{
		for (const auto& actor : m_actorsPrimary)
		{
			if (actor->GetName() == name)
				return actor;
		}

		return _World::emptyActor;
	}

	const shared_ptr<Actor>& World::Actor_GetByID(unsigned int ID)
	{
		for (const auto& actor : m_actorsPrimary)
		{
			if (actor->GetID() == ID)
				return actor;
		}

		return _World::emptyActor;
	}
	//===================================================================================================

	//= COMMON ACTOR CREATION ========================================================================
	shared_ptr<Actor>& World::CreateSkybox()
	{
		shared_ptr<Actor>& skybox = Actor_Create();
		skybox->SetName("Skybox");
		skybox->SetHierarchyVisibility(false);
		skybox->AddComponent<Skybox>();	

		return skybox;
	}

	shared_ptr<Actor>& World::CreateCamera()
	{
		auto resourceMng		= m_context->GetSubsystem<ResourceManager>();
		string scriptDirectory	= resourceMng->GetStandardResourceDirectory(Resource_Script);

		shared_ptr<Actor>& camera = Actor_Create();
		camera->SetName("Camera");
		camera->AddComponent<Camera>();
		camera->AddComponent<AudioListener>();
		camera->AddComponent<Script>()->SetScript(scriptDirectory + "MouseLook.as");
		camera->AddComponent<Script>()->SetScript(scriptDirectory + "FirstPersonController.as");
		camera->GetTransform_PtrRaw()->SetPositionLocal(Vector3(0.0f, 1.0f, -5.0f));

		return camera;
	}

	shared_ptr<Actor>& World::CreateDirectionalLight()
	{
		shared_ptr<Actor>& light = Actor_Create();
		light->SetName("DirectionalLight");
		light->GetTransform_PtrRaw()->SetRotationLocal(Quaternion::FromEulerAngles(30.0f, 0.0, 0.0f));
		light->GetTransform_PtrRaw()->SetPosition(Vector3(0.0f, 10.0f, 0.0f));

		Light* lightComp = light->AddComponent<Light>().get();
		lightComp->SetLightType(LightType_Directional);
		lightComp->SetIntensity(1.5f);

		return light;
	}
	//================================================================================================
}
