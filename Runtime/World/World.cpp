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
#include "Components/Renderable.h"
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
			bool started		= Engine::EngineMode_IsSet(Engine_Game) && m_wasInEditorMode;
			bool stopped		= !Engine::EngineMode_IsSet(Engine_Game) && !m_wasInEditorMode;
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
		
		m_entity_selected.reset();

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
		string filePath = filePathIn;
		if (FileSystem::GetExtensionFromFilePath(filePath) != EXTENSION_WORLD)
		{
			filePath += EXTENSION_WORLD;
		}

		// Save any in-memory changes done to resources while running.
		m_context->GetSubsystem<ResourceCache>()->SaveResourcesToFiles();

		// Create a prefab file
		auto file = make_unique<FileStream>(filePath, FileStreamMode_Write);
		if (!file->IsOpen())
		{
			return false;
		}

		// Save currently loaded resource paths
		vector<string> filePaths;
		m_context->GetSubsystem<ResourceCache>()->GetResourceFilePaths(filePaths);
		file->Write(filePaths);

		//= Save entities ============================
		// Only save root entities as they will also save their descendants
		vector<shared_ptr<Entity>> rootentities = Entities_GetRoots();

		// 1st - entity count
		auto rootentityCount = (int)rootentities.size();
		file->Write(rootentityCount);

		// 2nd - entity IDs
		for (const auto& root : rootentities)
		{
			file->Write(root->GetID());
		}

		// 3rd - entities
		for (const auto& root : rootentities)
		{
			root->Serialize(file.get());
		}
		//==============================================

		ProgressReport::Get().SetIsLoading(g_progress_Scene, false);
		LOG_INFO("Saving took " + to_string((int)timer.GetElapsedTimeMs()) + " ms");	
		FIRE_EVENT(Event_World_Saved);

		return true;
	}

	bool World::LoadFromFile(const string& filePath)
	{
		if (!FileSystem::FileExists(filePath))
		{
			LOG_ERROR(filePath + " was not found.");
			return false;
		}

		// Thread safety: Wait for scene and the renderer to stop the entities (could do double buffering in the future)
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
		auto resourceMng = m_context->GetSubsystem<ResourceCache>();
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

		//= Load entities ============================	
		// 1st - Root entity count
		int rootentityCount = file->ReadInt();

		// 2nd - Root entity IDs
		for (int i = 0; i < rootentityCount; i++)
		{
			auto& entity = Entity_Create();
			entity->SetID(file->ReadInt());
		}

		// 3rd - entities
		// It's important to loop with rootentityCount
		// as the vector size will increase as we deserialize
		// entities. This is because a entity will also
		// deserialize their descendants.
		for (int i = 0; i < rootentityCount; i++)
		{
			m_entitiesPrimary[i]->Deserialize(file.get(), nullptr);
		}
		//==============================================

		m_isDirty	= true;
		m_state		= Ticking;
		ProgressReport::Get().SetIsLoading(g_progress_Scene, false);	
		LOG_INFO("Loading took " + to_string((int)timer.GetElapsedTimeMs()) + " ms");	

		FIRE_EVENT(Event_World_Loaded);
		return true;
	}
	//===================================================================================================

	//= entity HELPER FUNCTIONS  ====================================================================
	shared_ptr<Entity>& World::Entity_Create()
	{
		auto entity = make_shared<Entity>(m_context);
		entity->Initialize(entity->AddComponent<Transform>().get());
		return m_entitiesPrimary.emplace_back(entity);
	}

	shared_ptr<Entity>& World::Entity_Add(const shared_ptr<Entity>& entity)
	{
		if (!entity)
			return m_entity_empty;

		return m_entitiesPrimary.emplace_back(entity);
	}

	bool World::entity_Exists(const shared_ptr<Entity>& entity)
	{
		if (!entity)
			return false;

		return Entity_GetByID(entity->GetID()) != nullptr;
	}

	// Removes an entity and all of it's children
	void World::Entity_Remove(const shared_ptr<Entity>& entity)
	{
		if (!entity)
			return;

		// If the entity to be removed is the entity that is currently selected, make sure to lose the reference
		if (entity->GetID() == m_entity_selected->GetID())
		{
			m_entity_selected = nullptr;
		}

		// remove any descendants
		vector<Transform*> children = entity->GetTransform_PtrRaw()->GetChildren();
		for (const auto& child : children)
		{
			Entity_Remove(child->GetEntity_PtrShared());
		}

		// Keep a reference to it's parent (in case it has one)
		Transform* parent = entity->GetTransform_PtrRaw()->GetParent();

		// Remove this entity
		for (auto it = m_entitiesPrimary.begin(); it < m_entitiesPrimary.end();)
		{
			shared_ptr<Entity> temp = *it;
			if (temp->GetID() == entity->GetID())
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

	vector<shared_ptr<Entity>> World::Entities_GetRoots()
	{
		vector<shared_ptr<Entity>> rootentities;
		for (const auto& entity : m_entitiesPrimary)
		{
			if (entity->GetTransform_PtrRaw()->IsRoot())
			{
				rootentities.emplace_back(entity);
			}
		}

		return rootentities;
	}

	const shared_ptr<Entity>& World::Entity_GetByName(const string& name)
	{
		for (const auto& entity : m_entitiesPrimary)
		{
			if (entity->GetName() == name)
				return entity;
		}

		return m_entity_empty;
	}

	const shared_ptr<Entity>& World::Entity_GetByID(unsigned int ID)
	{
		for (const auto& entity : m_entitiesPrimary)
		{
			if (entity->GetID() == ID)
				return entity;
		}

		return m_entity_empty;
	}

	void World::Pickentity()
	{
		auto camera = m_context->GetSubsystem<Renderer>()->GetCamera();
		if (!camera)
			return;

		if (camera->Pick(m_input->GetMousePosition(), m_entity_selected))
		{
			FIRE_EVENT_DATA(Event_World_EntitySelected, m_entity_selected);
		}
	}

	//===================================================================================================

	//= COMMON entity CREATION ========================================================================
	shared_ptr<Entity>& World::CreateSkybox()
	{
		shared_ptr<Entity>& skybox = Entity_Create();
		skybox->SetName("Skybox");
		skybox->SetHierarchyVisibility(false);
		skybox->AddComponent<Skybox>();	

		return skybox;
	}

	shared_ptr<Entity> World::CreateCamera()
	{
		auto resourceMng		= m_context->GetSubsystem<ResourceCache>();
		string scriptDirectory	= resourceMng->GetStandardResourceDirectory(Resource_Script);

		auto entity = Entity_Create();
		entity->SetName("Camera");
		entity->AddComponent<Camera>();
		entity->AddComponent<AudioListener>();
		entity->AddComponent<Script>()->SetScript(scriptDirectory + "MouseLook.as");
		entity->AddComponent<Script>()->SetScript(scriptDirectory + "FirstPersonController.as");
		entity->GetTransform_PtrRaw()->SetPositionLocal(Vector3(0.0f, 1.0f, -5.0f));

		return entity;
	}

	shared_ptr<Entity>& World::CreateDirectionalLight()
	{
		shared_ptr<Entity>& light = Entity_Create();
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
