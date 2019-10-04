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

//= INCLUDES ==================
#include "Widget_Viewport.h"
#include "../ImGui_Extension.h"
#include "Core/Timer.h"
#include "Rendering\Model.h"
//=============================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

Widget_Viewport::Widget_Viewport(Context* context) : Widget(context)
{
	m_title     = "Viewport";
    m_size      = Vector2(400, 250);
	m_flags     |= ImGuiWindowFlags_NoScrollbar;
    m_padding   = Vector2(4.0f);
    m_renderer  = m_context->GetSubsystem<Renderer>().get();
    m_world     = m_context->GetSubsystem<World>().get();
}

void Widget_Viewport::Tick()
{
	if (!m_renderer)
		return;
	
	// Get current frame window resolution
	uint32_t width			= static_cast<uint32_t>(ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x);
	uint32_t height			= static_cast<uint32_t>(ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y);
	const uint32_t max_res	= m_renderer->GetMaxResolution();
	if (width > max_res || height > max_res)
		return;

	// Make pixel perfect
	width	-= (width	% 2 != 0) ? 1 : 0;
	height	-= (height	% 2 != 0) ? 1 : 0;

	// Update engine's viewport
	m_renderer->viewport_editor_offset = Vector2(ImGui::GetWindowPos()) + m_window_padding;
	m_viewport.width	= static_cast<float>(width);
	m_viewport.height	= static_cast<float>(height);
	m_renderer->SetViewport(m_viewport);

	// Update engine's resolution
	if (m_timeSinceLastResChange >= 0.1f) // Don't stress the GPU too much
	{
		const auto& current_resolution = m_renderer->GetResolution();
		if (current_resolution.x != width || current_resolution.y != height) // Change only when needed
		{
            m_renderer->SetResolution(width, height);
			m_timeSinceLastResChange = 0;
		}
	}
	m_timeSinceLastResChange += m_context->GetSubsystem<Timer>()->GetDeltaTimeSec();

	// Draw the image after a potential Renderer::SetResolution() call has been made
	ImGuiEx::Image
	(
        m_renderer->GetFrameTexture().get(),
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
