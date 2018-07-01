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

//= INCLUDES =======================================
#include "Widget_Scene.h"
#include "Widget_Properties.h"
#include "../../ImGui/Source/imgui.h"
#include "../DragDrop.h"
#include "../EditorHelper.h"
#include "Core/Engine.h"
#include "Input/Backend_Def.h"
#include "Input/Backend_Imp.h"
#include "Rendering/RI/Backend_Def.h"
#include "Rendering/RI/Backend_Imp.h"
#include "Rendering/RI/D3D11//D3D11_RenderTexture.h"
#include "Scene/Scene.h"
#include "Scene/Actor.h"
#include "Scene/Components/Transform.h"
#include "Scene/Components/Light.h"
#include "Scene/Components/AudioSource.h"
#include "Scene/Components/AudioListener.h"
#include "Scene/Components/RigidBody.h"
#include "Scene/Components/Collider.h"
#include "Scene/Components/Camera.h"
#include "Scene/Components/Constraint.h"
#include "Scene/Components/Renderable.h"
//==================================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

weak_ptr<Actor> Widget_Scene::m_actorSelected;
weak_ptr<Actor> g_actorEmpty;

namespace HierarchyStatics
{
	static Actor* g_hoveredActor	= nullptr;
	static Engine* g_engine			= nullptr;
	static Scene* g_scene			= nullptr;
	static Input* g_input			= nullptr;
	static DragDropPayload g_payload;
}

Widget_Scene::Widget_Scene()
{
	m_title						= "Scene";
	m_context					= nullptr;
	HierarchyStatics::g_scene	= nullptr;
}

void Widget_Scene::Initialize(Context* context)
{
	Widget::Initialize(context);

	HierarchyStatics::g_engine = m_context->GetSubsystem<Engine>();
	HierarchyStatics::g_scene = m_context->GetSubsystem<Scene>();
	HierarchyStatics::g_input = m_context->GetSubsystem<Input>();

	m_windowFlags |= ImGuiWindowFlags_HorizontalScrollbar;
}

void Widget_Scene::Update()
{
	// If something is being loaded, don't parse the hierarchy
	if (EditorHelper::Get().GetEngineLoading())
		return;
	
	Tree_Show();
}

void Widget_Scene::SetSelectedActor(weak_ptr<Actor> actor)
{
	m_actorSelected = actor;
	Widget_Properties::Inspect(m_actorSelected);
}

void Widget_Scene::Tree_Show()
{
	OnTreeBegin();

	if (ImGui::TreeNodeEx("Root", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Dropping on the scene node should unparent the actor
		if (auto payload = DragDrop::Get().GetPayload(DragPayload_actor))
		{
			auto actorID = get<unsigned int>(payload->data);
			if (auto droppedGameObj = HierarchyStatics::g_scene->GetActorByID(actorID).lock())
			{
				droppedGameObj->GetTransform_PtrRaw()->SetParent(nullptr);
			}
		}

		auto rootactors = HierarchyStatics::g_scene->GetRootActors();
		for (const auto& actor : rootactors)
		{
			Tree_AddActor(actor.lock().get());
		}

		ImGui::TreePop();
	}

	OnTreeEnd();
}

void Widget_Scene::OnTreeBegin()
{
	HierarchyStatics::g_hoveredActor = nullptr;
}

void Widget_Scene::OnTreeEnd()
{
	HandleKeyShortcuts();
	HandleClicking();
	ContextMenu();
}

void Widget_Scene::Tree_AddActor(Actor* actor)
{
	// Node self visibility
	if (!actor->IsVisibleInHierarchy())
		return;

	// Node children visibility
	bool hasVisibleChildren = false;
	auto children = actor->GetTransform_PtrRaw()->GetChildren();
	for (const auto& child : children)
	{
		if (child->Getactor_PtrRaw()->IsVisibleInHierarchy())
		{
			hasVisibleChildren = true;
			break;
		}
	}

	ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_AllowItemOverlap;
	node_flags |= hasVisibleChildren ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf; // Expandable?	
	if (!m_actorSelected.expired()) // Selected?
	{
		node_flags |= (m_actorSelected.lock()->GetID() == actor->GetID()) ? ImGuiTreeNodeFlags_Selected : 0;
	}
	bool isNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)actor->GetID(), node_flags, actor->GetName().c_str());

	// Manually detect some useful states
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
	{
		HierarchyStatics::g_hoveredActor = actor;
	}

	HandleDragDrop(actor);	

	// Recursively show all child nodes
	if (isNodeOpen)
	{
		if (hasVisibleChildren)
		{
			for (const auto& child : children)
			{
				if (!child->Getactor_PtrRaw()->IsVisibleInHierarchy())
					continue;

				Tree_AddActor(child->Getactor_PtrRaw());
			}
		}

		// Pop if isNodeOpen
		ImGui::TreePop();
	}
}

void Widget_Scene::HandleClicking()
{
	// Since we are handling clicking manually, we must ensure we are inside the window
	if (!ImGui::IsMouseHoveringWindow())
		return;	

	// Left click on item - Select
	if (ImGui::IsMouseClicked(0) && HierarchyStatics::g_hoveredActor)
	{
		SetSelectedActor(HierarchyStatics::g_hoveredActor->GetPtrShared());
	}

	// Right click on item - Select and show context menu
	if (ImGui::IsMouseClicked(1))
	{
		if (HierarchyStatics::g_hoveredActor)
		{			
			SetSelectedActor(HierarchyStatics::g_hoveredActor->GetPtrShared());
		}

		ImGui::OpenPopup("##HierarchyContextMenu");		
	}

	// Clicking on empty space - Clear selection
	if ((ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) && !HierarchyStatics::g_hoveredActor)
	{
		SetSelectedActor(g_actorEmpty);
	}
}

void Widget_Scene::HandleDragDrop(Actor* actorPtr)
{
	// Drag
	if (DragDrop::Get().DragBegin())
	{
		HierarchyStatics::g_payload.data = actorPtr->GetID();
		HierarchyStatics::g_payload.type = DragPayload_actor;
		DragDrop::Get().DragPayload(HierarchyStatics::g_payload);
		DragDrop::Get().DragEnd();
	}
	// Drop
	if (auto payload = DragDrop::Get().GetPayload(DragPayload_actor))
	{
		auto actorID = get<unsigned int>(payload->data);
		if (auto droppedGameObj = HierarchyStatics::g_scene->GetActorByID(actorID).lock())
		{
			if (droppedGameObj->GetID() != actorPtr->GetID())
			{
				droppedGameObj->GetTransform_PtrRaw()->SetParent(actorPtr->GetTransform_PtrRaw());
			}
		}
	}
}

void Widget_Scene::ContextMenu()
{
	if (!ImGui::BeginPopup("##HierarchyContextMenu"))
		return;

	if (!m_actorSelected.expired())
	{
		ImGui::MenuItem("Rename");

		if (ImGui::MenuItem("Delete", "Delete"))
		{
			Action_actor_Delete(m_actorSelected);
		}
		ImGui::Separator();
	}

	// EMPTY
	if (ImGui::MenuItem("Create Empty"))
	{
		Action_actor_CreateEmpty();
	}

	// 3D OBJECCTS
	if (ImGui::BeginMenu("3D Objects"))
	{
		if (ImGui::MenuItem("Cube"))
		{
			Action_actor_CreateCube();
		}
		else if (ImGui::MenuItem("Quad"))
		{
			Action_actor_CreateQuad();
		}
		else if (ImGui::MenuItem("Sphere"))
		{
			Action_actor_CreateSphere();
		}
		else if (ImGui::MenuItem("Cylinder"))
		{
			Action_actor_CreateCylinder();
		}
		else if (ImGui::MenuItem("Cone"))
		{
			Action_actor_CreateCone();
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

void Widget_Scene::HandleKeyShortcuts()
{
	if (HierarchyStatics::g_input->GetButtonKeyboard(Delete))
	{
		Action_actor_Delete(m_actorSelected);
	}
}

void Widget_Scene::Action_actor_Delete(weak_ptr<Actor> actor)
{
	HierarchyStatics::g_scene->Actor_Remove(actor);
}

Actor* Widget_Scene::Action_actor_CreateEmpty()
{
	auto actor = HierarchyStatics::g_scene->Actor_CreateAdd().lock().get();
	if (auto selected = m_actorSelected.lock())
	{
		actor->GetTransform_PtrRaw()->SetParent(selected->GetTransform_PtrRaw());
	}

	return actor;
}

void Widget_Scene::Action_actor_CreateCube()
{
	auto actor = Action_actor_CreateEmpty();
	auto renderable = actor->AddComponent<Renderable>().lock();
	renderable->Geometry_Set(Geometry_Default_Cube);
	renderable->Material_UseDefault();
	actor->SetName("Cube");
}

void Widget_Scene::Action_actor_CreateQuad()
{
	auto actor = Action_actor_CreateEmpty();
	auto renderable = actor->AddComponent<Renderable>().lock();
	renderable->Geometry_Set(Geometry_Default_Quad);
	renderable->Material_UseDefault();
	actor->SetName("Quad");
}

void Widget_Scene::Action_actor_CreateSphere()
{
	auto actor = Action_actor_CreateEmpty();
	auto renderable = actor->AddComponent<Renderable>().lock();
	renderable->Geometry_Set(Geometry_Default_Sphere);
	renderable->Material_UseDefault();
	actor->SetName("Sphere");
}

void Widget_Scene::Action_actor_CreateCylinder()
{
	auto actor = Action_actor_CreateEmpty();
	auto renderable = actor->AddComponent<Renderable>().lock();
	renderable->Geometry_Set(Geometry_Default_Cylinder);
	renderable->Material_UseDefault();
	actor->SetName("Cylinder");
}

void Widget_Scene::Action_actor_CreateCone()
{
	auto actor = Action_actor_CreateEmpty();
	auto renderable = actor->AddComponent<Renderable>().lock();
	renderable->Geometry_Set(Geometry_Default_Cone);
	renderable->Material_UseDefault();
	actor->SetName("Cone");
}

void Widget_Scene::Action_actor_CreateCamera()
{
	auto actor = Action_actor_CreateEmpty();
	actor->AddComponent<Camera>();
	actor->SetName("Camera");
}

void Widget_Scene::Action_actor_CreateLightDirectional()
{
	auto actor = Action_actor_CreateEmpty();
	actor->AddComponent<Light>().lock()->SetLightType(LightType_Directional);
	actor->SetName("Directional");
}

void Widget_Scene::Action_actor_CreateLightPoint()
{
	auto actor = Action_actor_CreateEmpty();
	actor->AddComponent<Light>().lock()->SetLightType(LightType_Point);
	actor->SetName("Point");
}

void Widget_Scene::Action_actor_CreateLightSpot()
{
	auto actor = Action_actor_CreateEmpty();
	actor->AddComponent<Light>().lock()->SetLightType(LightType_Spot);
	actor->SetName("Spot");
}

void Widget_Scene::Action_actor_CreateRigidBody()
{
	auto actor = Action_actor_CreateEmpty();
	actor->AddComponent<RigidBody>();
	actor->SetName("RigidBody");
}

void Widget_Scene::Action_actor_CreateCollider()
{
	auto actor = Action_actor_CreateEmpty();
	actor->AddComponent<Collider>();
	actor->SetName("Collider");
}

void Widget_Scene::Action_actor_CreateConstraint()
{
	auto actor = Action_actor_CreateEmpty();
	actor->AddComponent<Constraint>();
	actor->SetName("Constraint");
}

void Widget_Scene::Action_actor_CreateAudioSource()
{
	auto actor = Action_actor_CreateEmpty();
	actor->AddComponent<AudioSource>();
	actor->SetName("AudioSource");
}

void Widget_Scene::Action_actor_CreateAudioListener()
{
	auto actor = Action_actor_CreateEmpty();
	actor->AddComponent<AudioListener>();
	actor->SetName("AudioListener");
}
