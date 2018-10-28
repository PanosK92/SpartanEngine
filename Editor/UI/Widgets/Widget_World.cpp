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

//= INCLUDES ==============================
#include "Widget_World.h"
#include "Widget_Properties.h"
#include "../DragDrop.h"
#include "../../ImGui/imgui_stdlib.h"
#include "Input/Input.h"
#include "Resource/ProgressReport.h"
#include "World/Actor.h"
#include "World/Components/Transform.h"
#include "World/Components/Light.h"
#include "World/Components/AudioSource.h"
#include "World/Components/AudioListener.h"
#include "World/Components/RigidBody.h"
#include "World/Components/Collider.h"
#include "World/Components/Camera.h"
#include "World/Components/Constraint.h"
#include "World/Components/Renderable.h"
//=========================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

weak_ptr<Actor> Widget_World::m_actorSelected;
weak_ptr<Actor> g_actorEmpty;

namespace SceneHelper
{
	static Engine* g_engine			= nullptr;
	static World* g_scene			= nullptr;
	static Input* g_input			= nullptr;
	static bool g_popupRenameActor	= false;
	static DragDropPayload g_payload;
	// Actors in relation to mouse events
	static Actor* g_actorCopied		= nullptr;
	static Actor* g_actorHovered	= nullptr;
	static Actor* g_actorClicked	= nullptr;
}

Widget_World::Widget_World(Context* context) : Widget(context)
{
	m_title					= "World";
	SceneHelper::g_scene	= nullptr;

	SceneHelper::g_engine	= m_context->GetSubsystem<Engine>();
	SceneHelper::g_scene	= m_context->GetSubsystem<World>();
	SceneHelper::g_input	= m_context->GetSubsystem<Input>();

	m_windowFlags |= ImGuiWindowFlags_HorizontalScrollbar;
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

	// On left click, select actor but only on release
	if (ImGui::IsMouseReleased(0) && SceneHelper::g_actorClicked)
	{
		// Make sure that the mouse was released while on the same actor
		if (SceneHelper::g_actorHovered && SceneHelper::g_actorHovered->GetID() == SceneHelper::g_actorClicked->GetID())
		{
			SetSelectedActor(SceneHelper::g_actorClicked->GetPtrShared());
		}
		SceneHelper::g_actorClicked = nullptr;
	}
}

void Widget_World::SetSelectedActor(weak_ptr<Actor> actor)
{
	m_actorSelected = actor;
	Widget_Properties::Inspect(m_actorSelected);
}

void Widget_World::Tree_Show()
{
	OnTreeBegin();

	if (ImGui::TreeNodeEx("Root", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Dropping on the scene node should unparent the actor
		if (auto payload = DragDrop::Get().GetPayload(DragPayload_Actor))
		{
			auto actorID = get<unsigned int>(payload->data);
			if (auto droppedActor = SceneHelper::g_scene->GetActorByID(actorID).lock())
			{
				droppedActor->GetTransform_PtrRaw()->SetParent(nullptr);
			}
		}

		auto rootActors = SceneHelper::g_scene->GetRootActors();
		for (const auto& actor : rootActors)
		{
			Tree_AddActor(actor.lock().get());
		}

		ImGui::TreePop();
	}

	OnTreeEnd();
}

void Widget_World::OnTreeBegin()
{
	SceneHelper::g_actorHovered = nullptr;
}

void Widget_World::OnTreeEnd()
{
	HandleKeyShortcuts();
	HandleClicking();
	Popups();
}

void Widget_World::Tree_AddActor(Actor* actor)
{
	if (!actor)
		return;

	bool isVisibleInHierarchy	= actor->IsVisibleInHierarchy();
	bool hasParent				= actor->GetTransform_PtrRaw()->HasParent();
	bool hasVisibleChildren		= false;

	// Don't draw invisible actors
	if (!isVisibleInHierarchy)
		return;

	// Determine children visibility
	auto children = actor->GetTransform_PtrRaw()->GetChildren();
	for (const auto& child : children)
	{
		if (child->GetActor_PtrRaw()->IsVisibleInHierarchy())
		{
			hasVisibleChildren = true;
			break;
		}
	}

	ImGuiTreeNodeFlags node_flags	= ImGuiTreeNodeFlags_AllowItemOverlap;
	node_flags						|= hasVisibleChildren ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf; // Expandable?	
	if (!m_actorSelected.expired()) // Selected?
	{
		node_flags |= (m_actorSelected.lock()->GetID() == actor->GetID()) ? ImGuiTreeNodeFlags_Selected : 0;
	}
	bool isNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)actor->GetID(), node_flags, actor->GetName().c_str());

	// Manually detect some useful states
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
	{
		SceneHelper::g_actorHovered = actor;
	}

	Actor_HandleDragDrop(actor);	

	// Recursively show all child nodes
	if (isNodeOpen)
	{
		if (hasVisibleChildren)
		{
			for (const auto& child : children)
			{
				if (!child->GetActor_PtrRaw()->IsVisibleInHierarchy())
					continue;

				Tree_AddActor(child->GetActor_PtrRaw());
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
	if (leftClick && SceneHelper::g_actorHovered)
	{
		SceneHelper::g_actorClicked	= SceneHelper::g_actorHovered;
	}

	// Right click on item - Select and show context menu
	if (ImGui::IsMouseClicked(1))
	{
		if (SceneHelper::g_actorHovered)
		{			
			SetSelectedActor(SceneHelper::g_actorHovered->GetPtrShared());
		}

		ImGui::OpenPopup("##HierarchyContextMenu");
	}

	// Clicking on empty space - Clear selection
	if ((leftClick || rightClick) && !SceneHelper::g_actorHovered)
	{
		SetSelectedActor(g_actorEmpty);
	}
}

void Widget_World::Actor_HandleDragDrop(Actor* actorPtr)
{
	// Drag
	if (DragDrop::Get().DragBegin())
	{
		SceneHelper::g_payload.data = actorPtr->GetID();
		SceneHelper::g_payload.type = DragPayload_Actor;
		DragDrop::Get().DragPayload(SceneHelper::g_payload);
		DragDrop::Get().DragEnd();
	}
	// Drop
	if (auto payload = DragDrop::Get().GetPayload(DragPayload_Actor))
	{
		auto actorID = get<unsigned int>(payload->data);
		if (auto droppedActor = SceneHelper::g_scene->GetActorByID(actorID).lock())
		{
			if (droppedActor->GetID() != actorPtr->GetID())
			{
				droppedActor->GetTransform_PtrRaw()->SetParent(actorPtr->GetTransform_PtrRaw());
			}
		}
	}
}

void Widget_World::Popups()
{
	Popup_ContextMenu();
	Popup_ActorRename();
}

void Widget_World::Popup_ContextMenu()
{
	if (!ImGui::BeginPopup("##HierarchyContextMenu"))
		return;

	bool onActor = !m_actorSelected.expired();

	if (onActor) if (ImGui::MenuItem("Copy"))
	{
		SceneHelper::g_actorCopied = m_actorSelected.lock().get();
	}

	if (ImGui::MenuItem("Paste"))
	{
		if (SceneHelper::g_actorCopied)
		{
			SceneHelper::g_actorCopied->Clone();
		}
	}

	if (onActor) if (ImGui::MenuItem("Rename"))
	{
		SceneHelper::g_popupRenameActor = true;
	}

	if (onActor) if (ImGui::MenuItem("Delete", "Delete"))
	{
		Action_Actor_Delete(m_actorSelected);
	}
	ImGui::Separator();

	// EMPTY
	if (ImGui::MenuItem("Create Empty"))
	{
		Action_Actor_CreateEmpty();
	}

	// 3D OBJECCTS
	if (ImGui::BeginMenu("3D Objects"))
	{
		if (ImGui::MenuItem("Cube"))
		{
			Action_Actor_CreateCube();
		}
		else if (ImGui::MenuItem("Quad"))
		{
			Action_Actor_CreateQuad();
		}
		else if (ImGui::MenuItem("Sphere"))
		{
			Action_Actor_CreateSphere();
		}
		else if (ImGui::MenuItem("Cylinder"))
		{
			Action_Actor_CreateCylinder();
		}
		else if (ImGui::MenuItem("Cone"))
		{
			Action_Actor_CreateCone();
		}

		ImGui::EndMenu();
	}

	// CAMERA
	if (ImGui::MenuItem("Camera"))
	{
		Action_actor_CreateCamera();
	}

	// LIGHT
	if (ImGui::BeginMenu("Light"))
	{
		if (ImGui::MenuItem("Directional"))
		{
			Action_actor_CreateLightDirectional();
		}
		else if (ImGui::MenuItem("Point"))
		{
			Action_actor_CreateLightPoint();
		}
		else if (ImGui::MenuItem("Spot"))
		{
			Action_actor_CreateLightSpot();
		}

		ImGui::EndMenu();
	}

	// PHYSICS
	if (ImGui::BeginMenu("Physics"))
	{
		if (ImGui::MenuItem("Rigid Body"))
		{
			Action_actor_CreateRigidBody();
		}
		else if (ImGui::MenuItem("Collider"))
		{
			Action_actor_CreateCollider();
		}
		else if (ImGui::MenuItem("Constraint"))
		{
			Action_actor_CreateConstraint();
		}

		ImGui::EndMenu();
	}

	// AUDIO
	if (ImGui::BeginMenu("Audio"))
	{
		if (ImGui::MenuItem("Audio Source"))
		{
			Action_actor_CreateAudioSource();
		}
		else if (ImGui::MenuItem("Audio Listener"))
		{
			Action_actor_CreateAudioListener();
		}

		ImGui::EndMenu();
	}

	ImGui::EndPopup();
}

void Widget_World::Popup_ActorRename()
{
	if (SceneHelper::g_popupRenameActor)
	{
		ImGui::OpenPopup("##RenameActor");
		SceneHelper::g_popupRenameActor = false;
	}

	if (ImGui::BeginPopup("##RenameActor"))
	{
		auto actor = m_actorSelected.lock();
		if (!actor)
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		string name = actor->GetName();

		ImGui::Text("Name:");
		ImGui::InputText("##edit", &name);
		actor->SetName(string(name));

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
	if (SceneHelper::g_input->GetButtonKeyboard(Delete))
	{
		Action_Actor_Delete(m_actorSelected);
	}
}

void Widget_World::Action_Actor_Delete(weak_ptr<Actor> actor)
{
	SceneHelper::g_scene->Actor_Remove(actor);
}

Actor* Widget_World::Action_Actor_CreateEmpty()
{
	auto actor = SceneHelper::g_scene->Actor_CreateAdd().lock().get();
	if (auto selected = m_actorSelected.lock())
	{
		actor->GetTransform_PtrRaw()->SetParent(selected->GetTransform_PtrRaw());
	}

	return actor;
}

void Widget_World::Action_Actor_CreateCube()
{
	auto actor = Action_Actor_CreateEmpty();
	auto renderable = actor->AddComponent<Renderable>().lock();
	renderable->Geometry_Set(Geometry_Default_Cube);
	renderable->Material_UseDefault();
	actor->SetName("Cube");
}

void Widget_World::Action_Actor_CreateQuad()
{
	auto actor = Action_Actor_CreateEmpty();
	auto renderable = actor->AddComponent<Renderable>().lock();
	renderable->Geometry_Set(Geometry_Default_Quad);
	renderable->Material_UseDefault();
	actor->SetName("Quad");
}

void Widget_World::Action_Actor_CreateSphere()
{
	auto actor = Action_Actor_CreateEmpty();
	auto renderable = actor->AddComponent<Renderable>().lock();
	renderable->Geometry_Set(Geometry_Default_Sphere);
	renderable->Material_UseDefault();
	actor->SetName("Sphere");
}

void Widget_World::Action_Actor_CreateCylinder()
{
	auto actor = Action_Actor_CreateEmpty();
	auto renderable = actor->AddComponent<Renderable>().lock();
	renderable->Geometry_Set(Geometry_Default_Cylinder);
	renderable->Material_UseDefault();
	actor->SetName("Cylinder");
}

void Widget_World::Action_Actor_CreateCone()
{
	auto actor = Action_Actor_CreateEmpty();
	auto renderable = actor->AddComponent<Renderable>().lock();
	renderable->Geometry_Set(Geometry_Default_Cone);
	renderable->Material_UseDefault();
	actor->SetName("Cone");
}

void Widget_World::Action_actor_CreateCamera()
{
	auto actor = Action_Actor_CreateEmpty();
	actor->AddComponent<Camera>();
	actor->SetName("Camera");
}

void Widget_World::Action_actor_CreateLightDirectional()
{
	auto actor = Action_Actor_CreateEmpty();
	actor->AddComponent<Light>().lock()->SetLightType(LightType_Directional);
	actor->SetName("Directional");
}

void Widget_World::Action_actor_CreateLightPoint()
{
	auto actor = Action_Actor_CreateEmpty();
	actor->AddComponent<Light>().lock()->SetLightType(LightType_Point);
	actor->SetName("Point");
}

void Widget_World::Action_actor_CreateLightSpot()
{
	auto actor = Action_Actor_CreateEmpty();
	actor->AddComponent<Light>().lock()->SetLightType(LightType_Spot);
	actor->SetName("Spot");
}

void Widget_World::Action_actor_CreateRigidBody()
{
	auto actor = Action_Actor_CreateEmpty();
	actor->AddComponent<RigidBody>();
	actor->SetName("RigidBody");
}

void Widget_World::Action_actor_CreateCollider()
{
	auto actor = Action_Actor_CreateEmpty();
	actor->AddComponent<Collider>();
	actor->SetName("Collider");
}

void Widget_World::Action_actor_CreateConstraint()
{
	auto actor = Action_Actor_CreateEmpty();
	actor->AddComponent<Constraint>();
	actor->SetName("Constraint");
}

void Widget_World::Action_actor_CreateAudioSource()
{
	auto actor = Action_Actor_CreateEmpty();
	actor->AddComponent<AudioSource>();
	actor->SetName("AudioSource");
}

void Widget_World::Action_actor_CreateAudioListener()
{
	auto actor = Action_Actor_CreateEmpty();
	actor->AddComponent<AudioListener>();
	actor->SetName("AudioListener");
}
