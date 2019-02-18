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

//= INCLUDES =======================
#include "Widget_Viewport.h"
#include "Rendering/Renderer.h"
#include "World/Entity.h"
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

namespace _Widget_Viewport
{
	static Renderer* g_renderer	= nullptr;
	static World* g_world		= nullptr;
	float g_windowPadding		= 4.0f;
}

Widget_Viewport::Widget_Viewport(Context* context) : Widget(context)
{
	m_title						= "Viewport";
	m_timeSinceLastResChange	= 0.0f;

	m_windowFlags |= ImGuiWindowFlags_NoScrollbar;
	_Widget_Viewport::g_renderer	= m_context->GetSubsystem<Renderer>().get();
	_Widget_Viewport::g_world		= m_context->GetSubsystem<World>().get();
	m_xMin = 400;
	m_yMin = 250;
}

bool Widget_Viewport::Begin()
{
	ImGui::SetNextWindowSize(ImVec2(m_xMin, m_yMin), ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(_Widget_Viewport::g_windowPadding, _Widget_Viewport::g_windowPadding));
	ImGui::Begin(m_title.c_str(), &m_isVisible, m_windowFlags);

	return true;
}

void Widget_Viewport::Tick(float deltaTime)
{
	if (!_Widget_Viewport::g_renderer)
		return;
	
	ShowFrame(deltaTime);
	ImGui::PopStyleVar();
}

void Widget_Viewport::ShowFrame(float deltaTime)
{
	// Get current frame window resolution
	unsigned int width	= (unsigned int)(ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x);
	unsigned int height = (unsigned int)(ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y);
	unsigned int maxRes = _Widget_Viewport::g_renderer->GetMaxResolution();
	if (width > maxRes || height > maxRes)
		return;

	// Make pixel perfect
	width	-= (width	% 2 != 0) ? 1 : 0;
	height	-= (height	% 2 != 0) ? 1 : 0;

	ImGui::Image(
		_Widget_Viewport::g_renderer->GetFrameShaderResource(),
		ImVec2((float)width, (float)height),
		ImVec2(0, 0),
		ImVec2(1, 1),
		ImColor(255, 255, 255, 255),
		ImColor(50, 127, 166, 255)
	);

	// Update engine's viewport
	_Widget_Viewport::g_renderer->viewport_editorOffset = EditorHelper::ToVector2(ImGui::GetWindowPos()) + Vector2(_Widget_Viewport::g_windowPadding);
	_Widget_Viewport::g_renderer->SetViewport(RHI_Viewport(0.0f, 0.0f, (float)width, (float)height));

	// Update engine's resolution
	if (m_timeSinceLastResChange >= 0.250f) // Don't stress the GPU too much
	{
		_Widget_Viewport::g_renderer->SetResolution(width, height);
		m_timeSinceLastResChange = 0;
	}
	m_timeSinceLastResChange += deltaTime;

	// If this widget was clicked, make the engine pick an entity
	if (ImGui::IsItemClicked(0))
	{
		_Widget_Viewport::g_world->Pickentity();
	}

	// Handle model drop
	if (auto payload = DragDrop::Get().GetPayload(DragPayload_Model))
	{
		EditorHelper::Get().LoadModel(get<const char*>(payload->data));
	}
}