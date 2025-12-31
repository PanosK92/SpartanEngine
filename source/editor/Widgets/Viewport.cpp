/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES =============================
#include "pch.h"
#include "Viewport.h"
#include "AssetBrowser.h"
#include "WorldViewer.h"
#include "RHI/RHI_Device.h"
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/ImGui_TransformGizmo.h"
#include "Settings.h"
//========================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

namespace
{
    bool first_frame         = true;
    uint32_t width_previous  = 0;
    uint32_t height_previous = 0;
}

Viewport::Viewport(Editor* editor) : Widget(editor)
{
    m_title         = "Viewport";
    m_size_initial  = Vector2(400, 250);
    m_flags        |= ImGuiWindowFlags_NoScrollbar;
    m_padding       = Vector2(2.0f);
}

void Viewport::OnTickVisible()
{
    // get viewport size
    uint32_t width  = static_cast<uint32_t>(ImGui::GetContentRegionAvail().x);
    uint32_t height = static_cast<uint32_t>(ImGui::GetContentRegionAvail().y);

    // update engine's viewport
    static bool resolution_set = Settings::HasLoadedUserSettingsFromFile();
    if (!first_frame) // during the first frame the viewport is not yet initialized (it's size will be something weird)
    {
        if (width_previous != width || height_previous != height)
        {
            if (RHI_Device::IsValidResolution(width, height))
            {
                Renderer::SetViewport(static_cast<float>(width), static_cast<float>(height)); 
                
                if (!resolution_set)
                {
                    // only set the render and output resolutions once
                    // they are expensive operations and we don't want to do it frequently
                    Renderer::SetResolutionOutput(width, height);

                    resolution_set = true;
                }

                width_previous  = width;
                height_previous = height;
            }
        }
    }
    first_frame = false;

    // let the input system know about the position of this viewport within the editor
    // this will allow the system to properly calculate a relative mouse position
    Vector2 offset = ImGui::GetCursorPos();
    offset.y += 34; // TODO: this is probably the tab bar height, find a way to get it properly
    Input::SetEditorViewportOffset(offset);

    // draw the image after a potential resolution change call has been made
    ImGuiSp::image(Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output), ImVec2(static_cast<float>(width), static_cast<float>(height)));

    // let the input system know if the mouse is within the viewport
    Input::SetMouseIsInViewport(ImGui::IsItemHovered());

    // handle model drop
    if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Model))
    {
        m_editor->GetWidget<AssetBrowser>()->ShowMeshImportDialog(get<const char*>(payload->data));
    }

    Camera* camera = World::GetCamera();

    // double-click to focus on entity
    if (camera && ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered() && ImGui::TransformGizmo::allow_picking())
    {
        camera->Pick();
        m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(camera->GetSelectedEntity());
        if (camera->GetSelectedEntity())
        {
            camera->FocusOnSelectedEntity();
        }
    }
    // mouse picking (with multi-select via Ctrl handled in Pick())
    else if (camera && ImGui::IsMouseClicked(0) && ImGui::IsItemHovered() && ImGui::TransformGizmo::allow_picking())
    {
        camera->Pick();
        // update the world viewer to reflect selection (uses primary selected entity for Properties)
        m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(camera->GetSelectedEntity());
    }

    // entity transform gizmo (will only show if entities have been picked)
    if (Renderer::GetOption<bool>(spartan::Renderer_Option::TransformHandle))
    {
        if (camera) // skip if no camera
        {
            const std::vector<spartan::Entity*>& selected_entities = camera->GetSelectedEntities();
            if (!selected_entities.empty()) // skip if no entities are selected
            {
                // use the first selected entity for direction check
                spartan::Entity* primary_selected = selected_entities[0];
                if (primary_selected)
                {
                    spartan::Entity* camera_entity = camera->GetEntity();
                    spartan::math::Vector3 dir_to_entity = primary_selected->GetPosition() - camera_entity->GetPosition();
                    dir_to_entity.Normalize();
                    if (dir_to_entity.Dot(camera_entity->GetForward()) >= 0.0f) // skip when the camera is facing away
                    {
                        ImGui::TransformGizmo::tick();
                    }
                }
            }
        }
    }

    // check if the engine wants cursor control
    if (camera && camera->GetFlag(spartan::CameraFlags::IsControlled))
    {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    else
    {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    }
}
