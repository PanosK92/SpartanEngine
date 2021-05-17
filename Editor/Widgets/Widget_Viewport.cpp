/*
Copyright(c) 2016-2021 Panos Karabelas

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
    m_input     = m_context->GetSubsystem<Input>();
}

void Widget_Viewport::TickVisible()
{
    if (!m_renderer)
        return;

    // Get size
    float width     = static_cast<float>(ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x);
    float height    = static_cast<float>(ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y);

    // Update engine's viewport
    if (m_width != width || m_height != height)
    {
        m_renderer->SetViewport(width, height);

        m_width     = width;
        m_height    = height;
    }

    // Let the input system know about the position of this viewport within the editor.
    // This will allow the system to properly calculate a relative mouse position.
    Vector2 offset = ImGui::GetCursorPos();
    offset.y += 34; // TODO: this is probably the tab bar height, find a way to get it properly
    m_input->SetEditorViewportOffset(offset);

    // Draw the image after a potential Renderer::SetResolution() call has been made
    ImGuiEx::Image
    (
        m_renderer->GetFrameTexture(),
        ImVec2(static_cast<float>(m_width), static_cast<float>(m_height)),
        ImColor(255, 255, 255, 255),
        ImColor(50, 127, 166, 255)
    );

    // Let the input system now if the mouse is within the viewport
    m_input->SetMouseIsInViewport(ImGui::IsItemHovered());

    // If this widget was released, make the engine pick an entity.
    if (ImGui::IsMouseDown(0) && ImGui::IsItemHovered())
    {
        EditorHelper::Get().PickEntity();
    }

    // Handle model drop
    if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayload_Model))
    {
        EditorHelper::Get().LoadModel(get<const char*>(payload->data));
    }
}
