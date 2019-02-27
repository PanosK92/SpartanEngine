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

//= INCLUDES ==========================
#include "World.h"
#include "Entity.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/Script.h"
#include "Components/Skybox.h"
#include "Components/AudioListener.h"
#include "../Core/Engine.h"
#include "../Core/Stopwatch.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/ProgressReport.h"
#include "../IO/FileStream.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Input/Input.h"
//=====================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	World::World(Context* context) : ISubsystem(context)
	{
		m_isDirty	= true;
		m_state		= Ticking;
		
		// Subscribe to events
		SUBSCRIBE_TO_EVENT(Event_World_Resolve, [this](Variant) { m_isDirty = true; });
		SUBSCRIBE_TO_EVENT(Event_World_Stop,	[this](Variant)	{ m_state = Idle; });
		SUBSCRIBE_TO_EVENT(Event_World_Start,	[this](Variant)	{ m_state = Ticking; });
	}

	World::~World()
	{
		Unload();
	}

	bool World::Initialize()
	{
		m_input		= m_context->GetSubsystem<Input>().get();
		m_profiler	= m_context->GetSubsystem<Profiler>().get();

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

		TIME_BLOCK_START_CPU(m_profiler);
		
		// Tick entities
		{
			// Detect game toggling
			const auto started	= Engine::EngineMode_IsSet(Engine_Game) && m_wasInEditorMode;
			const auto stopped	= !Engine::EngineMode_IsSet(Engine_Game) && !m_wasInEditorMode;
			m_wasInEditorMode	= !Engine::EngineMode_IsSet(Engine_Game);

			// Start
			if (started)
			{
				for (const auto& entity : m_entitiesPrimary)
				{
					entity->Start();
				}
			}
			// Stop
			if (stopped)
			{
				for (const auto& entity : m_entitiesPrimary)
				{
					entity->Stop();
				}
			}
			// Tick
			for (const auto& entity : m_entitiesPrimary)
			{
				entity->Tick();
			}
		}

		TIME_BLOCK_END_CPU(m_profiler);

		if (m_isDirty)
		{
			m_entitiesSecondary = m_entitiesPrimary;
			// Submit to the Renderer
			FIRE_EVENT_DATA(Event_World_Submit, m_entitiesSecondary);
			m_isDirty = false;
		}
	}

	void World::Unload()
	{
		FIRE_EVENT(Event_World_Unload);

		m_entitiesPrimary.clear();
		m_entitiesPrimary.shrink_to_fit();

		m_isDirty = true;
		
		// Don't clear secondary m_entitiesSecondary as they might be used by the renderer
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
		auto file_path = filePathIn;
		if (FileSystem::GetExtensionFromFilePath(file_path) != EXTENSION_WORLD)
		{
			file_path += EXTENSION_WORLD;
		}

		// Save any in-memory changes done to resources while running.
		m_context->GetSubsystem<ResourceCache>()->SaveResourcesToFiles();

		// Create a prefab file
		auto file = make_unique<FileStream>(file_path, FileStreamMode_Write);
		if (!file->IsOpen())
		{
			return false;
		}

		// Save currently loaded resource paths
		vector<string> file_paths;
		m_context->GetSubsystem<ResourceCache>()->GetResourceFilePaths(file_paths);
		file->Write(file_paths);

		//= Save entities ============================
		// Only save root entities as they will also save their descendants
		auto rootentities = EntitiesGetRoots();

		// 1st - entity count
		const auto root_entity_count = static_cast<unsigned int>(rootentities.size());
		file->Write(root_entity_count);

		// 2nd - entity IDs
		for (const auto& root : rootentities)
		{
			file->Write(root->GetId());
		}

		// 3rd - entities
		for (const auto& root : rootentities)
		{
			root->Serialize(file.get());
		}
		//==============================================

		ProgressReport::Get().SetIsLoading(g_progress_Scene, false);
		LOG_INFO("Saving took " + to_string(static_cast<int>(timer.GetElapsedTimeMs())) + " ms");	
		FIRE_EVENT(Event_World_Saved);

		return true;
	}

	bool World::LoadFromFile(const string& file_path)
	{
		if (!FileSystem::FileExists(file_path))
		{
			LOG_ERROR(file_path + " was not found.");
			return false;
		}

		// Thread safety: Wait for scene and the renderer to stop the entities (could do double buffering in the future)
		while (m_state != Loading || Renderer::IsRendering()) { m_state = Request_Loading; this_thread::sleep_for(chrono::milliseconds(16)); }

		ProgressReport::Get().Reset(g_progress_Scene);
		ProgressReport::Get().SetIsLoading(g_progress_Scene, true);
		ProgressReport::Get().SetStatus(g_progress_Scene, "Loading scene...");

		Unload();

		// Read all the resource file paths
		auto file = make_unique<FileStream>(file_path, FileStreamMode_Read);
		if (!file->IsOpen())
			return false;

		Stopwatch timer;

		vector<string> resource_paths;
		file->Read(&resource_paths);

		ProgressReport::Get().SetJobCount(g_progress_Scene, static_cast<unsigned int>(resource_paths.size()));

		// Load all the resources
		auto resource_mng = m_context->GetSubsystem<ResourceCache>();
		for (const auto& resource_path : resource_paths)
		{
			if (FileSystem::IsEngineModelFile(resource_path))
			{
				resource_mng->Load<Model>(resource_path);
			}

			if (FileSystem::IsEngineMaterialFile(resource_path))
			{
				resource_mng->Load<Material>(resource_path);
			}

			if (FileSystem::IsEngineTextureFile(resource_path))
			{
				resource_mng->Load<RHI_Texture>(resource_path);
			}

			ProgressReport::Get().IncrementJobsDone(g_progress_Scene);
		}

		//= Load entities ============================	
		// 1st - Root entity count
		const int root_entity_count = file->ReadUInt();

		// 2nd - Root entity IDs
		for (auto i = 0; i < root_entity_count; i++)
		{
			auto& entity = EntityCreate();
			entity->SetId(file->ReadInt());
		}

		// 3rd - entities
		// It's important to loop with root_entity_count
		// as the vector size will increase as we deserialize
		// entities. This is because a entity will also
		// deserialize their descendants.
		for (auto i = 0; i < root_entity_count; i++)
		{
			m_entitiesPrimary[i]->Deserialize(file.get(), nullptr);
		}
		//==============================================

		m_isDirty	= true;
		m_state		= Ticking;
		ProgressReport::Get().SetIsLoading(g_progress_Scene, false);	
		LOG_INFO("Loading took " + to_string(static_cast<int>(timer.GetElapsedTimeMs())) + " ms");	

		FIRE_EVENT(Event_World_Loaded);
		return true;
	}
	//===================================================================================================

	//= entity HELPER FUNCTIONS  ====================================================================
	shared_ptr<Entity>& World::EntityCreate()
	{
		auto entity = make_shared<Entity>(m_context);
		entity->Initialize(entity->AddComponent<Transform>().get());
		return m_entitiesPrimary.emplace_back(entity);
	}

	shared_ptr<Entity>& World::EntityAdd(const shared_ptr<Entity>& entity)
	{
		if (!entity)
			return m_entity_empty;

		return m_entitiesPrimary.emplace_back(entity);
	}

	bool World::EntityExists(const shared_ptr<Entity>& entity)
	{
		if (!entity)
			return false;

		return EntityGetById(entity->GetId()) != nullptr;
	}

	// Removes an entity and all of it's children
	void World::EntityRemove(const shared_ptr<Entity>& entity)
	{
		if (!entity)
			return;

		// remove any descendants
		auto children = entity->GetTransform_PtrRaw()->GetChildren();
		for (const auto& child : children)
		{
			EntityRemove(child->GetEntity_PtrShared());
		}

		// Keep a reference to it's parent (in case it has one)
		auto parent = entity->GetTransform_PtrRaw()->GetParent();

		// Remove this entity
		for (auto it = m_entitiesPrimary.begin(); it < m_entitiesPrimary.end();)
		{
			const auto temp = *it;
			if (temp->GetId() == entity->GetId())
			{
				it = m_entitiesPrimary.erase(it);
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

	vector<shared_ptr<Entity>> World::EntitiesGetRoots()
	{
		vector<shared_ptr<Entity>> rootEntities;
		for (const auto& entity : m_entitiesPrimary)
		{
			if (entity->GetTransform_PtrRaw()->IsRoot())
			{
				rootEntities.emplace_back(entity);
			}
		}

		return rootEntities;
	}

	const shared_ptr<Entity>& World::EntityGetByName(const string& name)
	{
		for (const auto& entity : m_entitiesPrimary)
		{
			if (entity->GetName() == name)
				return entity;
		}

		return m_entity_empty;
	}

	const shared_ptr<Entity>& World::EntityGetById(const unsigned int id)
	{
		for (const auto& entity : m_entitiesPrimary)
		{
			if (entity->GetId() == id)
				return entity;
		}

		return m_entity_empty;
	}
	//===================================================================================================

	//= COMMON ENTITY CREATION ========================================================================
	shared_ptr<Entity>& World::CreateSkybox()
	{
		auto& skybox = EntityCreate();
		skybox->SetName("Skybox");
		skybox->AddComponent<Skybox>();

		return skybox;
	}
	shared_ptr<Entity> World::CreateCamera()
	{
		auto resource_mng		= m_context->GetSubsystem<ResourceCache>();
		const auto dir_scripts	= resource_mng->GetDataDirectory(Asset_Scripts);

		auto entity = EntityCreate();
		entity->SetName("Camera");
		entity->AddComponent<Camera>();
		entity->AddComponent<AudioListener>();
		entity->AddComponent<Script>()->SetScript(dir_scripts + "MouseLook.as");
		entity->AddComponent<Script>()->SetScript(dir_scripts + "FirstPersonController.as");
		entity->GetTransform_PtrRaw()->SetPositionLocal(Vector3(0.0f, 1.0f, -5.0f));

		return entity;
	}

	shared_ptr<Entity>& World::CreateDirectionalLight()
	{
		auto& light = EntityCreate();
		light->SetName("DirectionalLight");
		light->GetTransform_PtrRaw()->SetRotationLocal(Quaternion::FromEulerAngles(30.0f, 0.0, 0.0f));
		light->GetTransform_PtrRaw()->SetPosition(Vector3(0.0f, 10.0f, 0.0f));

		auto light_comp = light->AddComponent<Light>().get();
		light_comp->SetLightType(LightType_Directional);
		light_comp->SetIntensity(1.5f);

		return light;
	}
	//================================================================================================
}
