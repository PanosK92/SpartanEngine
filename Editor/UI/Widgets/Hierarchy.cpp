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

//= INCLUDES ====================
#include "Hierarchy.h"
#include "../imgui/imgui.h"
#include "Scene/Scene.h"
#include "Scene/GameObject.h"
#include "Components/Transform.h"
#include "Core/Engine.h"
//===============================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

weak_ptr<GameObject> Hierarchy::m_gameObjectSelected;
weak_ptr<GameObject> g_gameObjectEmpty;
static Engine* g_engine = nullptr;
static Scene* g_scene	= nullptr;
static bool g_wasItemHovered = false;

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
}

void Hierarchy::Update()
{
	g_wasItemHovered = false;

	// If the engine is not updating, don't populate the hierarchy yet
	if (!g_engine->IsUpdating())
		return;

	if (ImGui::TreeNodeEx("Scene", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ImGui::GetFontSize() * 3); // Increase spacing to differentiate leaves from expanded contents.
		Tree_Populate();
		ImGui::PopStyleVar();
		ImGui::TreePop();
	}
}

void Hierarchy::Tree_Populate()
{
	auto rootGameObjects = g_scene->GetRootGameObjects();
	for (const auto& gameObject : rootGameObjects)
	{
		Tree_AddGameObject(gameObject);
	}
}

void Hierarchy::Tree_AddGameObject(const weak_ptr<GameObject>& currentGameObject)
{
	GameObject* gameObjPtr = currentGameObject.lock().get();

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
	ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;
	// Node flags -> Expandable?
	node_flags |= hasVisibleChildren ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf;
	// Node flags -> Selected?
	if (!m_gameObjectSelected.expired())
	{
		node_flags |= (m_gameObjectSelected.lock()->GetID() == gameObjPtr->GetID()) ? ImGuiTreeNodeFlags_Selected : 0;
	}

	// Node
	bool isNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)gameObjPtr->GetID(), node_flags, gameObjPtr->GetName().c_str());

	// Handle clicking
	if (ImGui::IsMouseHoveringWindow())
	{		
		// Left click
		if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_Default))
		{
			m_gameObjectSelected = currentGameObject;
			g_wasItemHovered = true;
		}

		// Right click
		if (ImGui::IsMouseClicked(1) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
		{
			m_gameObjectSelected = currentGameObject;
			ImGui::OpenPopup("##HierarchyContextMenu");
			g_wasItemHovered = true;
		}

		// Clicking inside the window (but not on an item)
		if ((ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) && !g_wasItemHovered)
		{
			m_gameObjectSelected = g_gameObjectEmpty;
		}
	}
	
	// Context menu
	if (ImGui::BeginPopup("##HierarchyContextMenu"))
	{
		if (!m_gameObjectSelected.expired())
		{
			ImGui::MenuItem("Rename");
			if (ImGui::MenuItem("Delete"))
			{
				GameObject_Delete(m_gameObjectSelected);
			}
			ImGui::Separator();
		}		
		if (ImGui::MenuItem("Creaty Empty"))
		{
			GameObject_CreateEmpty();
		}
		ImGui::EndPopup();
	}

	// Child nodes
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

void Hierarchy::GameObject_Delete(weak_ptr<GameObject> gameObject)
{
	g_scene->RemoveGameObject(gameObject);
}

void Hierarchy::GameObject_CreateEmpty()
{
	g_scene->CreateGameObject();
}
