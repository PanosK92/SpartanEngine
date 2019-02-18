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

//= INCLUDES ===============================
#include "Widget_World.h"
#include "Widget_Properties.h"
#include "../DragDrop.h"
#include "../../ImGui/Source/imgui_stdlib.h"
#include "Input/Input.h"
#include "Resource/ProgressReport.h"
#include "World/Entity.h"
#include "World/Components/Transform.h"
#include "World/Components/Light.h"
#include "World/Components/AudioSource.h"
#include "World/Components/AudioListener.h"
#include "World/Components/RigidBody.h"
#include "World/Components/Collider.h"
#include "World/Components/Camera.h"
#include "World/Components/Constraint.h"
#include "World/Components/Renderable.h"
//==========================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

namespace _Widget_World
{
	static World* g_world			= nullptr;
	static Input* g_input			= nullptr;
	static bool g_popupRenameentity	= false;
	static DragDropPayload g_payload;
	// entities in relation to mouse events
	static Entity* g_entityCopied	= nullptr;
	static Entity* g_entityHovered	= nullptr;
	static Entity* g_entityClicked	= nullptr;
}

Widget_World::Widget_World(Context* context) : Widget(context)
{
	m_title					= "World";
	_Widget_World::g_world	= m_context->GetSubsystem<World>().get();
	_Widget_World::g_input	= m_context->GetSubsystem<Input>().get();

	m_windowFlags |= ImGuiWindowFlags_HorizontalScrollbar;

	// Subscribe to entity clicked engine event
	SUBSCRIBE_TO_EVENT(Event_World_EntitySelected, [this](Variant data) { SetSelectedEntity(data.Get<shared_ptr<Entity>>(), false); });
}

void Widget_World::Tick(float deltaTime)
{
	// If something is being loaded, don't parse the hierarchy
	ProgressReport& progressReport	= ProgressReport::Get();
	bool isLoadingModel				= progressReport.GetIsLoading(g_progress_ModelImporter);
	bool isLoadingScene				= progressReport.GetIsLoading(g_progress_Scene);
	bool isLoading					= isLoadingModel || isLoadingScene;
	if (isLoading)
		return;
	
	Tree_Show();

	// On left click, select entity but only on release
	if (ImGui::IsMouseReleased(0) && _Widget_World::g_entityClicked)
	{
		// Make sure that the mouse was released while on the same entity
		if (_Widget_World::g_entityHovered && _Widget_World::g_entityHovered->GetID() == _Widget_World::g_entityClicked->GetID())
		{
			SetSelectedEntity(_Widget_World::g_entityClicked->GetPtrShared());
		}
		_Widget_World::g_entityClicked = nullptr;
	}
}

void Widget_World::Tree_Show()
{
	OnTreeBegin();

	if (ImGui::TreeNodeEx("Root", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Dropping on the scene node should unparent the entity
		if (auto payload = DragDrop::Get().GetPayload(DragPayload_entity))
		{
			auto entityID = get<unsigned int>(payload->data);
			if (auto droppedentity = _Widget_World::g_world->Entity_GetByID(entityID))
			{
				droppedentity->GetTransform_PtrRaw()->SetParent(nullptr);
			}
		}

		auto rootentities = _Widget_World::g_world->Entities_GetRoots();
		for (const auto& entity : rootentities)
		{
			Tree_AddEntity(entity.get());
		}

		ImGui::TreePop();
	}

	OnTreeEnd();
}

void Widget_World::OnTreeBegin()
{
	_Widget_World::g_entityHovered = nullptr;
}

void Widget_World::OnTreeEnd()
{
	HandleKeyShortcuts();
	HandleClicking();
	Popups();
}

void Widget_World::Tree_AddEntity(Entity* entity)
{
	if (!entity)
		return;

	bool isVisibleInHierarchy	= entity->IsVisibleInHierarchy();
	bool hasParent				= entity->GetTransform_PtrRaw()->HasParent();
	bool hasVisibleChildren		= false;

	// Don't draw invisible entities
	if (!isVisibleInHierarchy)
		return;

	// Determine children visibility
	auto children = entity->GetTransform_PtrRaw()->GetChildren();
	for (const auto& child : children)
	{
		if (child->GetEntity_PtrRaw()->IsVisibleInHierarchy())
		{
			hasVisibleChildren = true;
			break;
		}
	}

	// Flags
	ImGuiTreeNodeFlags node_flags	= ImGuiTreeNodeFlags_AllowItemOverlap;

	// Flag - Is expandable (has children) ?
	node_flags |= hasVisibleChildren ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf; 

	// Flag - Is selected?
	if (auto selectedentity = _Widget_World::g_world->GetSelectedentity())
	{
		bool isSelectedentity = selectedentity->GetID() == entity->GetID();
		node_flags |= isSelectedentity ? ImGuiTreeNodeFlags_Selected : 0;

		// Expand to show entity, if it was clicked during this frame
		if (m_expandToShowentity)
		{
			// If the selected entity is a descendant of the this entity, start expanding (this can happen if the user clicks on something in the 3D scene)
			if (selectedentity->GetTransform_PtrRaw()->IsDescendantOf(entity->GetTransform_PtrRaw()))
			{
				ImGui::SetNextTreeNodeOpen(true);

				// Stop expanding when we have reached the selected entity (it's visible to the user)
				if (isSelectedentity)
				{
					m_expandToShowentity = false;
				}
			}
		}
	}
	
	bool isNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)entity->GetID(), node_flags, entity->GetName().c_str());

	// Manually detect some useful states
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
	{
		_Widget_World::g_entityHovered = entity;
	}

	Entity_HandleDragDrop(entity);	

	// Recursively show all child nodes
	if (isNodeOpen)
	{
		if (hasVisibleChildren)
		{
			for (const auto& child : children)
			{
				if (!child->GetEntity_PtrRaw()->IsVisibleInHierarchy())
					continue;

				Tree_AddEntity(child->GetEntity_PtrRaw());
			}
		}

		// Pop if isNodeOpen
		ImGui::TreePop();
	}
}

void Widget_World::HandleClicking()
{
	bool isWindowHovered	= ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	bool leftClick			= ImGui::IsMouseClicked(0);
	bool rightClick			= ImGui::IsMouseClicked(1);

	// Since we are handling clicking manually, we must ensure we are inside the window
	if (!isWindowHovered)
		return;	

	// Left click on item - Don't select yet
	if (leftClick && _Widget_World::g_entityHovered)
	{
		_Widget_World::g_entityClicked	= _Widget_World::g_entityHovered;
	}

	// Right click on item - Select and show context menu
	if (ImGui::IsMouseClicked(1))
	{
		if (_Widget_World::g_entityHovered)
		{			
			SetSelectedEntity(_Widget_World::g_entityHovered->GetPtrShared());
		}

		ImGui::OpenPopup("##HierarchyContextMenu");
	}

	// Clicking on empty space - Clear selection
	if ((leftClick || rightClick) && !_Widget_World::g_entityHovered)
	{
		SetSelectedEntity(m_entity_empty);
	}
}

void Widget_World::Entity_HandleDragDrop(Entity* entityPtr)
{
	// Drag
	if (ImGui::BeginDragDropSource())
	{
		_Widget_World::g_payload.data = entityPtr->GetID();
		_Widget_World::g_payload.type = DragPayload_entity;
		DragDrop::Get().DragPayload(_Widget_World::g_payload);
		ImGui::EndDragDropSource();
	}
	// Drop
	if (auto payload = DragDrop::Get().GetPayload(DragPayload_entity))
	{
		auto entityID = get<unsigned int>(payload->data);
		if (auto droppedentity = _Widget_World::g_world->Entity_GetByID(entityID))
		{
			if (droppedentity->GetID() != entityPtr->GetID())
			{
				droppedentity->GetTransform_PtrRaw()->SetParent(entityPtr->GetTransform_PtrRaw());
			}
		}
	}
}

void Widget_World::SetSelectedEntity(std::shared_ptr<Directus::Entity> entity, bool fromEditor /*= true*/)
{
	m_expandToShowentity = true;

	// If the update comes from this widget, let the engine know about it
	if (fromEditor)
	{
		_Widget_World::g_world->SetSelectedentity(entity);
	}

	Widget_Properties::Inspect(entity);
}

void Widget_World::Popups()
{
	Popup_ContextMenu();
	Popup_EntityRename();
}

void Widget_World::Popup_ContextMenu()
{
	if (!ImGui::BeginPopup("##HierarchyContextMenu"))
		return;

	auto selectedentity	= _Widget_World::g_world->GetSelectedentity();
	bool onentity		= selectedentity != nullptr;

	if (onentity) if (ImGui::MenuItem("Copy"))
	{
		_Widget_World::g_entityCopied = selectedentity.get();
	}

	if (ImGui::MenuItem("Paste"))
	{
		if (_Widget_World::g_entityCopied)
		{
			_Widget_World::g_entityCopied->Clone();
		}
	}

	if (onentity) if (ImGui::MenuItem("Rename"))
	{
		_Widget_World::g_popupRenameentity = true;
	}

	if (onentity) if (ImGui::MenuItem("Delete", "Delete"))
	{
		Action_Entity_Delete(selectedentity);
	}
	ImGui::Separator();

	// EMPTY
	if (ImGui::MenuItem("Create Empty"))
	{
		Action_Entity_CreateEmpty();
	}

	// 3D OBJECCTS
	if (ImGui::BeginMenu("3D Objects"))
	{
		if (ImGui::MenuItem("Cube"))
		{
			Action_Entity_CreateCube();
		}
		else if (ImGui::MenuItem("Quad"))
		{
			Action_Entity_CreateQuad();
		}
		else if (ImGui::MenuItem("Sphere"))
		{
			Action_Entity_CreateSphere();
		}
		else if (ImGui::MenuItem("Cylinder"))
		{
			Action_Entity_CreateCylinder();
		}
		else if (ImGui::MenuItem("Cone"))
		{
			Action_Entity_CreateCone();
		}

		ImGui::EndMenu();
	}

	// CAMERA
	if (ImGui::MenuItem("Camera"))
	{
		Action_Entity_CreateCamera();
	}

	// LIGHT
	if (ImGui::BeginMenu("Light"))
	{
		if (ImGui::MenuItem("Directional"))
		{
			Action_Entity_CreateLightDirectional();
		}
		else if (ImGui::MenuItem("Point"))
		{
			Action_Entity_CreateLightPoint();
		}
		else if (ImGui::MenuItem("Spot"))
		{
			Action_Entity_CreateLightSpot();
		}

		ImGui::EndMenu();
	}

	// PHYSICS
	if (ImGui::BeginMenu("Physics"))
	{
		if (ImGui::MenuItem("Rigid Body"))
		{
			Action_Entity_CreateRigidBody();
		}
		else if (ImGui::MenuItem("Collider"))
		{
			Action_Entity_CreateCollider();
		}
		else if (ImGui::MenuItem("Constraint"))
		{
			Action_Entity_CreateConstraint();
		}

		ImGui::EndMenu();
	}

	// AUDIO
	if (ImGui::BeginMenu("Audio"))
	{
		if (ImGui::MenuItem("Audio Source"))
		{
			Action_Entity_CreateAudioSource();
		}
		else if (ImGui::MenuItem("Audio Listener"))
		{
			Action_Entity_CreateAudioListener();
		}

		ImGui::EndMenu();
	}

	ImGui::EndPopup();
}

void Widget_World::Popup_EntityRename()
{
	if (_Widget_World::g_popupRenameentity)
	{
		ImGui::OpenPopup("##RenameEntity");
		_Widget_World::g_popupRenameentity = false;
	}

	if (ImGui::BeginPopup("##RenameEntity"))
	{
		auto selectedentity = _Widget_World::g_world->GetSelectedentity();
		if (!selectedentity)
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		string name = selectedentity->GetName();

		ImGui::Text("Name:");
		ImGui::InputText("##edit", &name);
		selectedentity->SetName(string(name));

		if (ImGui::Button("Ok")) 
		{ 
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		ImGui::EndPopup();
	}
}

void Widget_World::HandleKeyShortcuts()
{
	if (_Widget_World::g_input->GetKey(Delete))
	{
		Action_Entity_Delete(_Widget_World::g_world->GetSelectedentity());
	}
}

void Widget_World::Action_Entity_Delete(shared_ptr<Entity> entity)
{
	_Widget_World::g_world->Entity_Remove(entity);
}

Entity* Widget_World::Action_Entity_CreateEmpty()
{
	auto entity = _Widget_World::g_world->Entity_Create().get();
	if (auto selectedentity = _Widget_World::g_world->GetSelectedentity())
	{
		entity->GetTransform_PtrRaw()->SetParent(selectedentity->GetTransform_PtrRaw());
	}

	return entity;
}

void Widget_World::Action_Entity_CreateCube()
{
	auto entity = Action_Entity_CreateEmpty();
	auto renderable = entity->AddComponent<Renderable>();
	renderable->Geometry_Set(Geometry_Default_Cube);
	renderable->Material_UseDefault();
	entity->SetName("Cube");
}

void Widget_World::Action_Entity_CreateQuad()
{
	auto entity = Action_Entity_CreateEmpty();
	auto renderable = entity->AddComponent<Renderable>();
	renderable->Geometry_Set(Geometry_Default_Quad);
	renderable->Material_UseDefault();
	entity->SetName("Quad");
}

void Widget_World::Action_Entity_CreateSphere()
{
	auto entity = Action_Entity_CreateEmpty();
	auto renderable = entity->AddComponent<Renderable>();
	renderable->Geometry_Set(Geometry_Default_Sphere);
	renderable->Material_UseDefault();
	entity->SetName("Sphere");
}

void Widget_World::Action_Entity_CreateCylinder()
{
	auto entity = Action_Entity_CreateEmpty();
	auto renderable = entity->AddComponent<Renderable>();
	renderable->Geometry_Set(Geometry_Default_Cylinder);
	renderable->Material_UseDefault();
	entity->SetName("Cylinder");
}

void Widget_World::Action_Entity_CreateCone()
{
	auto entity = Action_Entity_CreateEmpty();
	auto renderable = entity->AddComponent<Renderable>();
	renderable->Geometry_Set(Geometry_Default_Cone);
	renderable->Material_UseDefault();
	entity->SetName("Cone");
}

void Widget_World::Action_Entity_CreateCamera()
{
	auto entity = Action_Entity_CreateEmpty();
	entity->AddComponent<Camera>();
	entity->SetName("Camera");
}

void Widget_World::Action_Entity_CreateLightDirectional()
{
	auto entity = Action_Entity_CreateEmpty();
	entity->AddComponent<Light>()->SetLightType(LightType_Directional);
	entity->SetName("Directional");
}

void Widget_World::Action_Entity_CreateLightPoint()
{
	auto entity = Action_Entity_CreateEmpty();
	entity->AddComponent<Light>()->SetLightType(LightType_Point);
	entity->SetName("Point");
}

void Widget_World::Action_Entity_CreateLightSpot()
{
	auto entity = Action_Entity_CreateEmpty();
	entity->AddComponent<Light>()->SetLightType(LightType_Spot);
	entity->SetName("Spot");
}

void Widget_World::Action_Entity_CreateRigidBody()
{
	auto entity = Action_Entity_CreateEmpty();
	entity->AddComponent<RigidBody>();
	entity->SetName("RigidBody");
}

void Widget_World::Action_Entity_CreateCollider()
{
	auto entity = Action_Entity_CreateEmpty();
	entity->AddComponent<Collider>();
	entity->SetName("Collider");
}

void Widget_World::Action_Entity_CreateConstraint()
{
	auto entity = Action_Entity_CreateEmpty();
	entity->AddComponent<Constraint>();
	entity->SetName("Constraint");
}

void Widget_World::Action_Entity_CreateAudioSource()
{
	auto entity = Action_Entity_CreateEmpty();
	entity->AddComponent<AudioSource>();
	entity->SetName("AudioSource");
}

void Widget_World::Action_Entity_CreateAudioListener()
{
	auto entity = Action_Entity_CreateEmpty();
	entity->AddComponent<AudioListener>();
	entity->SetName("AudioListener");
}
