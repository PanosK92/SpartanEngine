/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Core/Timer.h"
#include "Core/Settings.h"
#include "Rendering/Model.h"
#include "../ImGui_Extension.h"
//=============================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

Widget_Viewport::Widget_Viewport(Editor* editor) : Widget(editor)
{
    m_title     = "Viewport";
    m_size      = Vector2(400, 250);
    m_flags     |= ImGuiWindowFlags_NoScrollbar;
    m_padding   = Vector2(4.0f);
    m_world     = m_context->GetSubsystem<World>();
    m_renderer  = m_context->GetSubsystem<Renderer>();
    m_settings  = m_context->GetSubsystem<Settings>();
}

void Widget_Viewport::Tick()
{
    if (!m_renderer)
        return;

    // Get current frame window resolution
    float width            = static_cast<float>(ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x);
    float height        = static_cast<float>(ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y);

    // Update engine's viewport
    Vector2 offset = Vector2(ImGui::GetWindowPos()) + m_window_padding;
    m_renderer->SetViewport(width, height, offset.x, offset.y);

    // If this is the first tick and the first time the engine runs (no settings file loaded), we set the resolution to match the viewport size.
    // This is to avoid a scenario were the resolution is much higher than what the user assumes, resulting in less performance.
    if (m_is_resolution_dirty && !m_settings->Loaded())
    {
        m_renderer->SetResolution(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        m_is_resolution_dirty = false;
    }

    // Draw the image after a potential Renderer::SetResolution() call has been made
    ImGuiEx::Image
    (
        m_renderer->GetFrameTexture(),
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
