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

#pragma once

//= INCLUDES ====================
#include "Hierarchy.h"
#include "../imgui/imgui.h"
#include "Scene/Scene.h"
#include "Scene/GameObject.h"
#include "Components/Transform.h"
//===============================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

weak_ptr<GameObject> Hierarchy::m_gameObjectSelected;
static Scene* g_scene = nullptr;

Hierarchy::Hierarchy()
{
	m_title = "Hierarchy";

	m_context = nullptr;
	g_scene = nullptr;
}

void Hierarchy::Initialize(Context* context)
{
	Widget::Initialize(context);
	g_scene = m_context->GetSubsystem<Scene>();
}

void Hierarchy::Update()
{
	if (ImGui::TreeNodeEx("Scene", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ImGui::GetFontSize() * 3); // Increase spacing to differentiate leaves from expanded contents.
		Populate();
		ImGui::PopStyleVar();
		ImGui::TreePop();
	}
}

void Hierarchy::Populate()
{
	if (!g_scene)
		return;

	auto rootGameObjects = g_scene->GetRootGameObjects();
	for (const auto& gameObject : rootGameObjects)
	{
		AddGameObject(gameObject);
	}
}

void Hierarchy::AddGameObject(weak_ptr<GameObject> gameObject)
{
	GameObject* gameObjPtr = gameObject.lock().get();

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

	// Node flags
	ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;
	node_flags |= hasVisibleChildren ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf;
	if (!m_gameObjectSelected.expired())
	{
		node_flags |= m_gameObjectSelected.lock()->GetID() == gameObjPtr->GetID() ? ImGuiTreeNodeFlags_Selected : 0;
	}

	// Node
	bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)gameObjPtr->GetID(), node_flags, gameObjPtr->GetName().c_str());
	if (ImGui::IsItemClicked())
	{
		m_gameObjectSelected = gameObject;
	}

	// Child nodes
	if (node_open)
	{
		if (hasVisibleChildren)
		{
			for (const auto& child : children)
			{
				if (!child->GetGameObject()->IsVisibleInHierarchy())
					continue;

				AddGameObject(child->GetGameObjectRef());
			}
		}
		ImGui::TreePop();
	}
}
