/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ============================================
#include "Viewport.h"
#include "AssetViewer.h"
#include "Core/Timer.h"
#include "Rendering/Renderer.h"
#include "Event.h"
#include "../Editor.h"
#include "../ImGui/ImGuiExtension.h"
#include "../ImGui/Implementation/ImGui_TransformGizmo.h"
#include "WorldViewer.h"
//=======================================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

Viewport::Viewport(Editor* editor) : Widget(editor)
{
    m_title        = "Viewport";
    m_size_initial = Vector2(400, 250);
    m_flags        |= ImGuiWindowFlags_NoScrollbar;
    m_padding      = Vector2(2.0f);
    m_world        = m_context->GetSystem<World>();
    m_renderer     = m_context->GetSystem<Renderer>();
    m_input        = m_context->GetSystem<Input>();

    //ImGui::TransformGizmo::apply_style();
}

void Viewport::TickVisible()
{
    if (!m_renderer)
        return;

    // Get size
    float width  = static_cast<float>(ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x);
    float height = static_cast<float>(ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y);

    // Update engine's viewport.
    // Skip the first frame though, because widgets are still being added and the reported width/height is not valid.
    if (m_renderer->GetFrameNum() >= 1)
    {
        if (m_width != width || m_height != height)
        {
            m_renderer->SetViewport(width, height);

            m_width  = width;
            m_height = height;
        }
    }

    // Let the input system know about the position of this viewport within the editor.
    // This will allow the system to properly calculate a relative mouse position.
    Vector2 offset = ImGui::GetCursorPos();
    offset.y += 34; // TODO: this is probably the tab bar height, find a way to get it properly
    m_input->SetEditorViewportOffset(offset);

    // Draw the image after a potential resolution change call has been made
    ImGui_SP::image(m_renderer->GetFrameTexture(), ImVec2(static_cast<float>(m_width), static_cast<float>(m_height)));

    // Let the input system know if the mouse is within the viewport
    m_input->SetMouseIsInViewport(ImGui::IsItemHovered());

    // Handle model drop
    if (auto payload = ImGui_SP::receive_drag_drop_payload(ImGui_SP::DragPayloadType::Model))
    {
        m_editor->GetWidget<AssetViewer>()->ShowMeshImportDialog(get<const char*>(payload->data));
    }

    // Mouse picking
    if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered() && ImGui::TransformGizmo::allow_picking())
    {
        m_renderer->GetCamera()->Pick();
        m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(m_renderer->GetCamera()->GetSelectedEntity());
    }

    // Entity transform gizmo (will only show if an entity has been picked)
    if (m_renderer->GetOption<bool>(Spartan::RendererOption::Debug_TransformHandle))
    {
        ImGui::TransformGizmo::tick(m_context);
    }
}
