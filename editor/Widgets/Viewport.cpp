/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "AssetBrowser.h"
#include "WorldViewer.h"
#include "RHI/RHI_Device.h"
#include "../ImGui/ImGuiExtension.h"
#include "../ImGui/Implementation/ImGui_TransformGizmo.h"
#include "Settings.h"
//=======================================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
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
    uint32_t width  = static_cast<uint32_t>(ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x);
    uint32_t height = static_cast<uint32_t>(ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y);

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
    ImGuiSp::image(Renderer::GetFrameTexture(), ImVec2(static_cast<float>(width), static_cast<float>(height)));

    // let the input system know if the mouse is within the viewport
    Input::SetMouseIsInViewport(ImGui::IsItemHovered());

    // handle model drop
    if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Model))
    {
        m_editor->GetWidget<AssetBrowser>()->ShowMeshImportDialog(get<const char*>(payload->data));
    }

    shared_ptr<Camera> camera = Renderer::GetCamera();

    // mouse picking
    if (camera && ImGui::IsMouseClicked(0) && ImGui::IsItemHovered() && ImGui::TransformGizmo::allow_picking())
    {
        camera->Pick();
        m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(camera->GetSelectedEntity());
    }

    // entity transform gizmo (will only show if an entity has been picked)
    if (Renderer::GetOption<bool>(Spartan::Renderer_Option::TransformHandle))
    {
        ImGui::TransformGizmo::tick();
    }

    // check if the engine wants cursor control
    if (camera && camera->IsActivelyControlled())
    {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    else
    {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    }
}
