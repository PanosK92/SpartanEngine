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

//= INCLUDES =======================
#include "Viewport.h"
#include "../imgui/imgui.h"
#include "Graphics/Renderer.h"
#include "Scene/Scene.h"
#include "Scene/GameObject.h"
#include "Scene/Components/Camera.h"
#include "../EditorHelper.h"
#include "Hierarchy.h"
//==================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
//=======================

static Renderer* g_renderer			= nullptr;
static Scene* g_scene				= nullptr;
static Vector2 g_framePos;

static bool g_physics				= true;
static bool g_aabb					= false;
static bool g_gizmos				= true;
static bool g_pickingRay			= false;
static bool g_grid					= true;
static bool g_performanceMetrics	= false;

const char* g_rendererViews[] =
{
	"Default",
	"Albedo",
	"Material",
	"Normal",
	"Depth"
};
static int g_rendererViewInt		= 0;
static const char* g_rendererView	= g_rendererViews[g_rendererViewInt];

Viewport::Viewport()
{
	m_title = "Viewport";
}

void Viewport::Initialize(Context* context)
{
	Widget::Initialize(context);
	m_windowFlags	|= ImGuiWindowFlags_NoScrollbar;
	g_renderer		= m_context->GetSubsystem<Renderer>();
	g_scene			= m_context->GetSubsystem<Scene>();
}

void Viewport::Update()
{
	if (!g_renderer)
		return;
	
	ShowTopToolbar();
	ImGui::Separator();
	ShowFrame();
}

void Viewport::ShowTopToolbar()
{
	// Render options
	ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x); ImGui::Checkbox("Physics", &g_physics);
	ImGui::SameLine(); ImGui::Checkbox("AABB", &g_aabb);
	ImGui::SameLine(); ImGui::Checkbox("Gizmos", &g_gizmos);
	ImGui::SameLine(); ImGui::Checkbox("Picking Ray", &g_pickingRay);
	ImGui::SameLine(); ImGui::Checkbox("Scene Grid", &g_grid);
	ImGui::SameLine(); ImGui::Checkbox("Performance Metrics", &g_performanceMetrics);

	// G-Buffer Visualization
	ImGui::SameLine(); ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 145); ImGui::Text("G-Buffer");
	ImGui::PushItemWidth(80);
	ImGui::SameLine(); if (ImGui::BeginCombo("##RendererConfig", g_rendererView))
	{
		for (int i = 0; i < IM_ARRAYSIZE(g_rendererViews); i++)
		{
			bool is_selected = (g_rendererView == g_rendererViews[i]);
			if (ImGui::Selectable(g_rendererViews[i], is_selected))
			{
				g_rendererView = g_rendererViews[i];
				g_rendererViewInt = i;
			}
			if (is_selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::PopItemWidth();

	SetRenderFlags();
}

void Viewport::ShowFrame()
{
	float width		= ImGui::GetWindowContentRegionWidth();
	float height	= ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y - 30;
	g_renderer->SetResolution((int)width, (int)height);
	g_renderer->SetViewport((int)width, (int)height);

	g_framePos = EditorHelper::ToVector2(ImGui::GetCursorPos()) + EditorHelper::ToVector2(ImGui::GetWindowPos());
	ImGui::Image(
		g_renderer->GetFrame(),
		ImVec2(width, height),
		ImVec2(0, 0),
		ImVec2(1, 1),
		ImColor(255, 255, 255, 255),
		ImColor(50, 127, 166, 255)
	);

	MousePicking();
}

void Viewport::MousePicking()
{
	if (!ImGui::IsMouseHoveringWindow() || !ImGui::IsMouseClicked(0))
		return;

	auto camera = g_scene->GetMainCamera();
	if (!camera.expired())
	{
		Vector2 mousePosRelative = EditorHelper::ToVector2(ImGui::GetMousePos()) - g_framePos;
		auto picked = camera.lock()->GetComponent<Camera>().lock()->Pick(mousePosRelative);
		if (!picked.expired())
		{
			Hierarchy::SetSelectedGameObject(picked);
			return;
		}
	}

	Hierarchy::SetSelectedGameObject(weak_ptr<GameObject>());
}
void Viewport::SetRenderFlags()
{
	auto flags = g_renderer->GetRenderFlags();

	flags = g_physics				? flags | Render_Physics			: flags & ~Render_Physics;
	flags = g_aabb					? flags | Render_AABB				: flags & ~Render_AABB;
	flags = g_gizmos				? flags | Render_Light				: flags & ~Render_Light;
	flags = g_pickingRay			? flags | Render_PickingRay			: flags & ~Render_PickingRay;
	flags = g_grid					? flags | Render_SceneGrid			: flags & ~Render_SceneGrid;
	flags = g_performanceMetrics	? flags | Render_PerformanceMetrics : flags & ~Render_PerformanceMetrics;	
	flags = g_rendererViewInt == 1	? flags | Render_Albedo				: flags & ~Render_Albedo;
	flags = g_rendererViewInt == 2	? flags | Render_Normal				: flags & ~Render_Normal;
	flags = g_rendererViewInt == 3	? flags | Render_Depth				: flags & ~Render_Depth;
	flags = g_rendererViewInt == 4	? flags | Render_Material			: flags & ~Render_Material;
	flags = g_rendererViewInt == 0	? flags & ~Render_Albedo & ~Render_Normal & ~Render_Depth & ~Render_Material : flags;

	g_renderer->SetRenderFlags(flags);
}
