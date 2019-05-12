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
#include "../ImGui_Extension.h"
//==================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
using namespace Math;
//=======================

namespace _Widget_Viewport
{
	static Renderer* g_renderer	= nullptr;
	static World* g_world		= nullptr;
	float g_window_padding		= 4.0f;
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
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(_Widget_Viewport::g_window_padding, _Widget_Viewport::g_window_padding));
	ImGui::Begin(m_title.c_str(), &m_isVisible, m_windowFlags);

	return true;
}

void Widget_Viewport::Tick(const float delta_time)
{
	if (!_Widget_Viewport::g_renderer)
		return;
	
	ShowFrame(delta_time);
	ImGui::PopStyleVar();
}

void Widget_Viewport::ShowFrame(const float delta_time)
{
	// Get current frame window resolution
	auto width			= static_cast<unsigned int>(ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x);
	auto height			= static_cast<unsigned int>(ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y);
	const auto max_res	= _Widget_Viewport::g_renderer->GetMaxResolution();
	if (width > max_res || height > max_res)
		return;

	// Make pixel perfect
	width	-= (width	% 2 != 0) ? 1 : 0;
	height	-= (height	% 2 != 0) ? 1 : 0;

	// Update engine's viewport
	_Widget_Viewport::g_renderer->viewport_editor_offset = Vector2(ImGui::GetWindowPos()) + _Widget_Viewport::g_window_padding;
	_Widget_Viewport::g_renderer->SetViewport(RHI_Viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)));

	// Update engine's resolution
	if (m_timeSinceLastResChange >= 0.1f) // Don't stress the GPU too much
	{
		_Widget_Viewport::g_renderer->SetResolution(width, height);
		m_timeSinceLastResChange = 0;
	}
	m_timeSinceLastResChange += delta_time;

	// Draw the image after a potential Renderer::SetResolution() call has been made
	ImGuiEx::Image
	(
		_Widget_Viewport::g_renderer->GetFrameTexture(),
		ImVec2(static_cast<float>(width), static_cast<float>(height)),
		ImColor(255, 255, 255, 255),
		ImColor(50, 127, 166, 255)
	);

	// If this widget was released, make the engine pick an entity.
	// Don't do that on mouse down as a mouse down event might also mean that the user is currently transforming the entity.
	if (ImGui::IsMouseReleased(0) && ImGui::IsItemHovered())
	{
		EditorHelper::Get().PickEntity();
	}

	// Handle model drop
	if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayload_Model))
	{
		EditorHelper::Get().LoadModel(get<const char*>(payload->data));
	}
}