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
#include "Widget_Scene.h"
#include "../../ImGui/imgui.h"
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
#include "Scene/Components/Renderable.h"
#include "Widget_Properties.h"
//=========================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

weak_ptr<GameObject> Widget_Scene::m_gameObjectSelected;
weak_ptr<GameObject> g_gameObjectEmpty;

namespace HierarchyStatics
{
	static GameObject* g_hoveredGameObject	= nullptr;
	static Engine* g_engine					= nullptr;
	static Scene* g_scene					= nullptr;
	static Input* g_input					= nullptr;
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

void Widget_Scene::SetSelectedGameObject(weak_ptr<GameObject> gameObject)
{
	m_gameObjectSelected = gameObject;
	Widget_Properties::Inspect(m_gameObjectSelected);
}

void Widget_Scene::Tree_Show()
{
	OnTreeBegin();

	if (ImGui::TreeNodeEx("Root", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Dropping on the scene node should unparent the GameObject
		if (auto payload = DragDrop::Get().GetPayload(DragPayload_GameObject))
		{
			auto gameObjectID = get<unsigned int>(payload->data);
			if (auto droppedGameObj = HierarchyStatics::g_scene->GetGameObjectByID(gameObjectID).lock())
			{
				droppedGameObj->GetTransformRef()->SetParent(nullptr);
			}
		}

		auto rootGameObjects = HierarchyStatics::g_scene->GetRootGameObjects();
		for (const auto& gameObject : rootGameObjects)
		{
			Tree_AddGameObject(gameObject.lock().get());
		}

		ImGui::TreePop();
	}

	OnTreeEnd();
}

void Widget_Scene::OnTreeBegin()
{
	HierarchyStatics::g_hoveredGameObject = nullptr;
}

void Widget_Scene::OnTreeEnd()
{
	HandleKeyShortcuts();
	HandleClicking();
	ContextMenu();
}

void Widget_Scene::Tree_AddGameObject(GameObject* gameObject)
{
	// Node self visibility
	if (!gameObject->IsVisibleInHierarchy())
		return;

	// Node children visibility
	bool hasVisibleChildren = false;
	auto children = gameObject->GetTransformRef()->GetChildren();
	for (const auto& child : children)
	{
		if (child->GetGameObject_Ref()->IsVisibleInHierarchy())
		{
			hasVisibleChildren = true;
			break;
		}
	}

	ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_AllowItemOverlap;
	node_flags |= hasVisibleChildren ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf; // Expandable?	
	if (!m_gameObjectSelected.expired()) // Selected?
	{
		node_flags |= (m_gameObjectSelected.lock()->GetID() == gameObject->GetID()) ? ImGuiTreeNodeFlags_Selected : 0;
	}
	bool isNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)gameObject->GetID(), node_flags, gameObject->GetName().c_str());

	// Manully detect some useful states
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
	{
		HierarchyStatics::g_hoveredGameObject = gameObject;
	}

	HandleDragDrop(gameObject);	

	// Recursively show all child nodes
	if (isNodeOpen)
	{
		if (hasVisibleChildren)
		{
			for (const auto& child : children)
			{
				if (!child->GetGameObject_Ref()->IsVisibleInHierarchy())
					continue;

				Tree_AddGameObject(child->GetGameObject_Ref());
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
	if (ImGui::IsMouseClicked(0) && HierarchyStatics::g_hoveredGameObject)
	{
		SetSelectedGameObject(HierarchyStatics::g_hoveredGameObject->GetSharedPtr());
	}

	// Right click on item - Select and show context menu
	if (ImGui::IsMouseClicked(1))
	{
		if (HierarchyStatics::g_hoveredGameObject)
		{			
			SetSelectedGameObject(HierarchyStatics::g_hoveredGameObject->GetSharedPtr());
		}

		ImGui::OpenPopup("##HierarchyContextMenu");		
	}

	// Clicking on empty space - Clear selection
	if ((ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) && !HierarchyStatics::g_hoveredGameObject)
	{
		SetSelectedGameObject(g_gameObjectEmpty);
	}
}

void Widget_Scene::HandleDragDrop(GameObject* gameObjPtr)
{
	// Drag
	if (DragDrop::Get().DragBegin())
	{
		HierarchyStatics::g_payload.data = gameObjPtr->GetID();
		HierarchyStatics::g_payload.type = DragPayload_GameObject;
		DragDrop::Get().DragPayload(HierarchyStatics::g_payload);
		DragDrop::Get().DragEnd();
	}
	// Drop
	if (auto payload = DragDrop::Get().GetPayload(DragPayload_GameObject))
	{
		auto gameObjectID = get<unsigned int>(payload->data);
		if (auto droppedGameObj = HierarchyStatics::g_scene->GetGameObjectByID(gameObjectID).lock())
		{
			if (droppedGameObj->GetID() != gameObjPtr->GetID())
			{
				droppedGameObj->GetTransformRef()->SetParent(gameObjPtr->GetTransformRef());
			}
		}
	}
}

void Widget_Scene::ContextMenu()
{
	if (!ImGui::BeginPopup("##HierarchyContextMenu"))
		return;

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
		else if (ImGui::MenuItem("Sphere"))
		{
			Action_GameObject_CreateSphere();
		}
		else if (ImGui::MenuItem("Cylinder"))
		{
			Action_GameObject_CreateCylinder();
		}
		else if (ImGui::MenuItem("Cone"))
		{
			Action_GameObject_CreateCone();
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

void Widget_Scene::HandleKeyShortcuts()
{
	if (HierarchyStatics::g_input->GetButtonKeyboard(Delete))
	{
		Action_GameObject_Delete(m_gameObjectSelected);
	}
}

void Widget_Scene::Action_GameObject_Delete(weak_ptr<GameObject> gameObject)
{
	HierarchyStatics::g_scene->GameObject_Remove(gameObject);
}

GameObject* Widget_Scene::Action_GameObject_CreateEmpty()
{
	auto gameObject = HierarchyStatics::g_scene->GameObject_CreateAdd().lock().get();
	if (auto selected = m_gameObjectSelected.lock())
	{
		gameObject->GetTransformRef()->SetParent(selected->GetTransformRef());
	}

	return gameObject;
}

void Widget_Scene::Action_GameObject_CreateCube()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	auto renderable = gameObject->AddComponent<Renderable>().lock();
	renderable->UseStandardMesh(MeshType_Cube);
	renderable->UseStandardMaterial();
	gameObject->SetName("Cube");
}

void Widget_Scene::Action_GameObject_CreateQuad()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	auto renderable = gameObject->AddComponent<Renderable>().lock();
	renderable->UseStandardMesh(MeshType_Quad);
	renderable->UseStandardMaterial();
	gameObject->SetName("Quad");
}

void Widget_Scene::Action_GameObject_CreateSphere()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	auto renderable = gameObject->AddComponent<Renderable>().lock();
	renderable->UseStandardMesh(MeshType_Sphere);
	renderable->UseStandardMaterial();
	gameObject->SetName("Sphere");
}

void Widget_Scene::Action_GameObject_CreateCylinder()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	auto renderable = gameObject->AddComponent<Renderable>().lock();
	renderable->UseStandardMesh(MeshType_Cylinder);
	renderable->UseStandardMaterial();
	gameObject->SetName("Cylinder");
}

void Widget_Scene::Action_GameObject_CreateCone()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	auto renderable = gameObject->AddComponent<Renderable>().lock();
	renderable->UseStandardMesh(MeshType_Cone);
	renderable->UseStandardMaterial();
	gameObject->SetName("Cone");
}

void Widget_Scene::Action_GameObject_CreateCamera()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Camera>();
	gameObject->SetName("Camera");
}

void Widget_Scene::Action_GameObject_CreateLightDirectional()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Light>().lock()->SetLightType(LightType_Directional);
	gameObject->SetName("Directional");
}

void Widget_Scene::Action_GameObject_CreateLightPoint()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Light>().lock()->SetLightType(LightType_Point);
	gameObject->SetName("Point");
}

void Widget_Scene::Action_GameObject_CreateLightSpot()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Light>().lock()->SetLightType(LightType_Spot);
	gameObject->SetName("Spot");
}

void Widget_Scene::Action_GameObject_CreateRigidBody()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<RigidBody>();
	gameObject->SetName("RigidBody");
}

void Widget_Scene::Action_GameObject_CreateCollider()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Collider>();
	gameObject->SetName("Collider");
}

void Widget_Scene::Action_GameObject_CreateConstraint()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<Constraint>();
	gameObject->SetName("Constraint");
}

void Widget_Scene::Action_GameObject_CreateAudioSource()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<AudioSource>();
	gameObject->SetName("AudioSource");
}

void Widget_Scene::Action_GameObject_CreateAudioListener()
{
	auto gameObject = Action_GameObject_CreateEmpty();
	gameObject->AddComponent<AudioListener>();
	gameObject->SetName("AudioListener");
}
