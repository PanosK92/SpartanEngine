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

//= INCLUDES =======================
#include "Widget_Viewport.h"
#include "Rendering/Renderer.h"
#include "World/Actor.h"
#include "World/Components/Camera.h"
#include "Widget_World.h"
#include "Core/Settings.h"
#include "../DragDrop.h"
//==================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
//=======================

namespace Widget_Viewport_Properties
{
	static Renderer* g_renderer	= nullptr;
	static World* g_scene		= nullptr;
	static Vector2 g_framePos;
}

Widget_Viewport::Widget_Viewport(Context* context) : Widget(context)
{
	m_title						= "Viewport";
	m_timeSinceLastResChange	= 0.0f;

	m_windowFlags |= ImGuiWindowFlags_NoScrollbar;
	Widget_Viewport_Properties::g_renderer	= m_context->GetSubsystem<Renderer>();
	Widget_Viewport_Properties::g_scene		= m_context->GetSubsystem<World>();
	m_xMin = 400;
	m_yMin = 250;
}

void Widget_Viewport::Begin()
{
	ImGui::SetNextWindowSize(ImVec2(m_xMin, m_yMin), ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
	ImGui::Begin(m_title.c_str(), &m_isVisible, m_windowFlags);
}

void Widget_Viewport::Tick(float deltaTime)
{
	if (!Widget_Viewport_Properties::g_renderer)
		return;
	
	ShowFrame(deltaTime);
	ImGui::PopStyleVar();
}

void Widget_Viewport::ShowFrame(float deltaTime)
{
	int width	= (int)(ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x);
	int height	= (int)(ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y);

	// Make sure we are pixel perfect
	width	-= (width	% 2 != 0) ? 1 : 0;
	height	-= (height	% 2 != 0) ? 1 : 0;

	// Display frame
	Widget_Viewport_Properties::g_framePos = EditorHelper::ToVector2(ImGui::GetCursorPos()) + EditorHelper::ToVector2(ImGui::GetWindowPos());
	ImGui::Image(
		Widget_Viewport_Properties::g_renderer->GetFrameShaderResource(),
		ImVec2((float)width, (float)height),
		ImVec2(0, 0),
		ImVec2(1, 1),
		ImColor(255, 255, 255, 255),
		ImColor(50, 127, 166, 255)
	);

	// Adjust resolution if necessary
	if (Settings::Get().GetResolutionWidth() != width || Settings::Get().GetResolutionHeight() != height)
	{
		if (m_timeSinceLastResChange >= 0.250f) // Don't stress the GPU too much
		{
			Widget_Viewport_Properties::g_renderer->SetResolution(width, height);
			m_timeSinceLastResChange = 0;
		}
	}
	m_timeSinceLastResChange += deltaTime;

	// Handle model drop
	if (auto payload = DragDrop::Get().GetPayload(DragPayload_Model))
	{
		EditorHelper::Get().LoadModel(get<const char*>(payload->data));
	}

	MousePicking();
}

void Widget_Viewport::MousePicking()
{
	if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || !ImGui::IsMouseClicked(0))
		return;

	auto camera = Widget_Viewport_Properties::g_scene->GetMainCamera();
	if (!camera.expired())
	{
		Vector2 mousePosRelative = EditorHelper::ToVector2(ImGui::GetMousePos()) - Widget_Viewport_Properties::g_framePos;
		auto picked = camera.lock()->GetComponent<Camera>().lock()->Pick(mousePosRelative);
		if (!picked.expired())
		{
			Widget_World::SetSelectedActor(picked);
			return;
		}
	}

	Widget_World::SetSelectedActor(weak_ptr<Actor>());
}