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
#include "Hierarchy.h"
#include "../imgui/imgui.h"
#include "../DragDrop.h"
#include "../EditorHelper.h"
#include "Scene/Scene.h"
#include "Scene/GameObject.h"
#include "Core/Engine.h"
#include "Input/DInput/DInput.h"
#include "Scene/Components/Transform.h"
#include "Scene/Components/Light.h"
#include "Scene/Components/AudioSource.h"
#include "Scene/Components/AudioListener.h"
#include "Scene/Components/RigidBody.h"
#include "Scene/Components/Collider.h"
#include "Scene/Components/Camera.h"
#include "Scene/Components/Constraint.h"
#include "Scene/Components/MeshFilter.h"
#include "Scene/Components/MeshRenderer.h"
//=========================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

weak_ptr<GameObject> Hierarchy::m_gameObjectSelected;
weak_ptr<GameObject> g_gameObjectEmpty;
static Engine* g_engine						= nullptr;
static Scene* g_scene						= nullptr;
static Input* g_input						= nullptr;
static bool g_itemHoveredOnPreviousFrame	= false;
static DragDropPayload g_payload;

Hierarchy::Hierarchy()
{
	m_title = "Hierarchy";

	m_context = nullptr;
	g_scene = nullptr;
}

void Hierarchy::Initialize(Context* context)
{
	Widget::Initialize(context);

	g_engine = m_context->GetSubsystem<Engine>();
	g_scene = m_context->GetSubsystem<Scene>();
	g_input = m_context->GetSubsystem<Input>();

	m_windowFlags |= ImGuiWindowFlags_HorizontalScrollbar;
}

void Hierarchy::Update()
{
	// If something is being loaded, don't parse the hierarchy
	if (EditorHelper::Get().GetEngineLoading())
		return;
	
	Tree_Show();
}

void Hierarchy::Tree_Show()
{
	OnTreeBegin();

	if (ImGui::TreeNodeEx("Scene", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Dropping on the scene node should unparent the GameObject
		auto drop = DragDrop::Get().GetPayload(g_dragDrop_Type_GameObject);
		if (drop.type == g_dragDrop_Type_GameObject)
		{
			auto gameObjectID = (unsigned int)drop.data;
			if (auto droppedGameObj = g_scene->GetGameObjectByID(gameObjectID).lock())
			{
				droppedGameObj->GetTransform()->SetParent(nullptr);
			}
		}

		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ImGui::GetFontSize() * 3); // Increase spacing to differentiate leaves from expanded contents.
		auto rootGameObjects = g_scene->GetRootGameObjects();
		for (const auto& gameObject : rootGameObjects)
		{
			Tree_AddGameObject(gameObject);
		}

		ImGui::PopStyleVar();
		ImGui::TreePop();
	}

	OnTreeEnd();
}

void Hierarchy::OnTreeBegin()
{
	g_itemHoveredOnPreviousFrame = false;
}

void Hierarchy::OnTreeEnd()
{
	HandleKeyShortcuts();
}

void Hierarchy::Tree_AddGameObject(const weak_ptr<GameObject>& gameObject)
{
	GameObject* gameObjPtr = gameObject.lock().get();
	if (!gameObjPtr)
		return;

	// Node self visibility
	if (!gameObjPtr->IsVisibleInHierarchy())
		return;

	// Node children visibility
	bool hasVisibleChildren = false;
	auto children = gameObjPtr->GetTransform()->GetChildren();
	for (const auto& child : children)
	{
		if (child->GetGameObject()->IsVisibleInHierarchy())
		{
			hasVisibleChildren = true;
			break;
		}
	}

	// Node flags -> Default
	ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_AllowItemOverlap;
	// Node flags -> Expandable?
	node_flags |= hasVisibleChildren ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf;
	// Node flags -> Selected?
	if (!m_gameObjectSelected.expired())
	{
		node_flags |= (m_gameObjectSelected.lock()->GetID() == gameObjPtr->GetID()) ? ImGuiTreeNodeFlags_Selected : 0;
	}

	// Node
	bool isNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)gameObjPtr->GetID(), node_flags, gameObjPtr->GetName().c_str());

	// Drag
	if (DragDrop::Get().DragBegin())
	{
		g_payload.data = (char*)gameObjPtr->GetID();
		g_payload.type = g_dragDrop_Type_GameObject;
		DragDrop::Get().DragPayload(g_payload);
		DragDrop::Get().DragEnd();
	}
	// Drop
	auto drop = DragDrop::Get().GetPayload(g_dragDrop_Type_GameObject);
	if (drop.type == g_dragDrop_Type_GameObject)
	{
		auto gameObjectID = (unsigned int)drop.data;
		if (auto droppedGameObj = g_scene->GetGameObjectByID(gameObjectID).lock())
		{
			if (droppedGameObj->GetID() != gameObjPtr->GetID())
			{
				droppedGameObj->GetTransform()->SetParent(gameObjPtr->GetTransform());
			}
		}
	}

	// Handle clicking
	if (ImGui::IsMouseHoveringWindow())
	{		
		// Left click
		if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_Default))
		{
			m_gameObjectSelected = gameObject;
			g_itemHoveredOnPreviousFrame = true;
		}

		// Right click
		if (ImGui::IsMouseClicked(1))
		{
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
			{
				m_gameObjectSelected = gameObject;
				g_itemHoveredOnPreviousFrame = true;
			}
			else
			{
				ImGui::OpenPopup("##HierarchyContextMenu");
			}			
		}

		// Clicking (any button) inside the window but not on an item (empty space)
		if ((ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) && !g_itemHoveredOnPreviousFrame)
		{
			m_gameObjectSelected = g_gameObjectEmpty;
		}
	}
	
	ContextMenu();

	// Recursively show all child nodes
	if (isNodeOpen)
	{
		if (hasVisibleChildren)
		{
			for (const auto& child : children)
			{
				if (!child->GetGameObject()->IsVisibleInHierarchy())
					continue;

				Tree_AddGameObject(child->GetGameObjectRef());
			}
		}
		ImGui::TreePop();
	}
}

void Hierarchy::ContextMenu()
{
	if (ImGui::BeginPopup("##HierarchyContextMenu"))
	{
		if (!m_gameObjectSelected.expired())
		{
			ImGui::MenuItem("Rename");
			if (ImGui::MenuItem("Delete", "Delete"))
			{
				Action_GameObject_Delete(m_gameObjectSelected);
			}
			ImGui::Separator();
		}

		// EMPTY
		if (ImGui::MenuItem("Creaty Empty"))
		{
			Action_GameObject_CreateEmpty();
		}

		// 3D OBJECCTS
		if (ImGui::BeginMenu("3D Objects"))
		{
			if (ImGui::MenuItem("Cube"))
			{
				Action_GameObject_CreateCube();
			}
			else if (ImGui::MenuItem("Quad"))
			{
				Action_GameObject_CreateQuad();
			}

			ImGui::EndMenu();
		}

		// CAMERA
		if (ImGui::MenuItem("Camera"))
		{
			Action_GameObject_CreateCamera();
		}

		// LIGHT
		if (ImGui::BeginMenu("Light"))
		{
			if (ImGui::MenuItem("Directional"))
			{
				Action_GameObject_CreateLightDirectional();
			}
			else if (ImGui::MenuItem("Point"))
			{
				Action_GameObject_CreateLightPoint();
			}
			else if (ImGui::MenuItem("Spot"))
			{
				Action_GameObject_CreateLightSpot();
			}

			ImGui::EndMenu();
		}

		// PHYSICS
		if (ImGui::BeginMenu("Physics"))
		{
			if (ImGui::MenuItem("Rigid Body"))
			{
				Action_GameObject_CreateRigidBody();
			}
			else if (ImGui::MenuItem("Collider"))
			{
				Action_GameObject_CreateCollider();
			}
			else if (ImGui::MenuItem("Constraint"))
			{
				Action_GameObject_CreateConstraint();
			}

			ImGui::EndMenu();
		}

		// AUDIO
		if (ImGui::BeginMenu("Audio"))
		{
			if (ImGui::MenuItem("Audio Source"))
			{
				Action_GameObject_CreateAudioSource();
			}
			else if (ImGui::MenuItem("Audio Listener"))
			{
				Action_GameObject_CreateAudioListener();
			}

			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}
}

void Hierarchy::HandleKeyShortcuts()
{
	if (g_input->GetButtonKeyboard(Delete))
	{
		Action_GameObject_Delete(m_gameObjectSelected);
	}
}

void Hierarchy::Action_GameObject_Delete(weak_ptr<GameObject> gameObject)
{
	g_scene->RemoveGameObject(gameObject);
}

GameObject* Hierarchy::Action_GameObject_CreateEmpty()
{
	auto gameObject = g_scene->CreateGameObject().lock().get();
	if (auto selected = m_gameObjectSelected.lock())
	{
		gameObject->GetTransform()->SetParent(selected->GetTransform());
	}

	return gameObject;
}

void Hierarchy::Action_GameObject_CreateCube()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<MeshFilter>().lock()->UseStandardMesh(MeshType_Cube);
	gameObject->AddComponent<MeshRenderer>().lock()->UseStandardMaterial();
	gameObject->SetName("Cube");
}

void Hierarchy::Action_GameObject_CreateQuad()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<MeshFilter>().lock()->UseStandardMesh(MeshType_Quad);
	gameObject->AddComponent<MeshRenderer>().lock()->UseStandardMaterial();
	gameObject->SetName("Quad");
}

void Hierarchy::Action_GameObject_CreateCamera()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Camera>();
	gameObject->SetName("Camera");
}

void Hierarchy::Action_GameObject_CreateLightDirectional()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Light>().lock()->SetLightType(LightType_Directional);
	gameObject->SetName("Directional");
}

void Hierarchy::Action_GameObject_CreateLightPoint()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Light>().lock()->SetLightType(LightType_Point);
	gameObject->SetName("Point");
}

void Hierarchy::Action_GameObject_CreateLightSpot()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Light>().lock()->SetLightType(LightType_Spot);
	gameObject->SetName("Spot");
}

void Hierarchy::Action_GameObject_CreateRigidBody()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<RigidBody>();
	gameObject->SetName("RigidBody");
}

void Hierarchy::Action_GameObject_CreateCollider()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Collider>();
	gameObject->SetName("Collider");
}

void Hierarchy::Action_GameObject_CreateConstraint()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Constraint>();
	gameObject->SetName("Constraint");
}

void Hierarchy::Action_GameObject_CreateAudioSource()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<AudioSource>();
	gameObject->SetName("AudioSource");
}

void Hierarchy::Action_GameObject_CreateAudioListener()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<AudioListener>();
	gameObject->SetName("AudioListener");
}
