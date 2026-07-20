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
#include "Properties.h"
#include "RHI/RHI_Device.h"
#include "Rendering/Renderer.h"
#include "Rendering/Material.h"
#include "Resource/ResourceCache.h"
#include "World/Prefab.h"
#include "World/Components/Render.h"
#include "World/Components/Camera.h"
#include "Math/Ray.h"
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/ImGui_Style.h"
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

    // material drag-preview state, while a material asset is being dragged over the viewport,
    // the mesh under the cursor temporarily wears that material, release commits, drag-away reverts
    // entity is tracked by id so a deletion mid-drag cannot dereference a dangling pointer on revert
    uint64_t             preview_entity_id            = 0;
    shared_ptr<Material> preview_original_material;
    bool                 preview_original_was_default = false;
    bool                 preview_drag_was_active      = false;

    // resolves the renderable under the mouse cursor using triangle precision,
    // aabb-only was insufficient because parent renderables (eg a gltf scene wrapper)
    // have aabbs that swallow the whole world so they always win the broadphase
    Entity* pick_entity_under_cursor()
    {
        Camera* camera = World::GetCamera();
        if (!camera)
        {
            return nullptr;
        }

        return camera->FindEntityUnderCursor();
    }

    void clear_preview_state()
    {
        preview_entity_id            = 0;
        preview_original_material.reset();
        preview_original_was_default = false;
    }

    void revert_material_preview()
    {
        if (preview_entity_id == 0)
        {
            return;
        }

        if (Entity* entity = World::GetEntityById(preview_entity_id))
        {
            if (Render* render = entity->GetComponent<Render>())
            {
                if (preview_original_was_default)
                {
                    render->SetDefaultMaterial();
                }
                else if (preview_original_material)
                {
                    render->SetMaterial(preview_original_material);
                }
            }
        }

        clear_preview_state();
    }

    void apply_material_preview(Entity* entity, const char* material_path)
    {
        if (!entity || !material_path || !*material_path)
        {
            return;
        }

        Render* render = entity->GetComponent<Render>();
        if (!render)
        {
            return;
        }

        // remember what to restore, the default flag short-circuits the path lookup for engine-owned defaults
        bool was_default = render->IsUsingDefaultMaterial();
        shared_ptr<Material> original;
        if (Material* current = render->GetMaterial(); current && !was_default)
        {
            original = ResourceCache::GetByPath<Material>(current->GetResourceFilePath());
        }

        // cache-aware load, no-op if the material is already in the resource cache
        shared_ptr<Material> dragged = ResourceCache::Load<Material>(material_path);
        if (!dragged)
        {
            return;
        }

        render->SetMaterial(dragged);

        preview_entity_id            = entity->GetObjectId();
        preview_original_material    = original;
        preview_original_was_default = was_default;
    }

    // peek at the active drag-drop payload without accepting, returns the path if it is a material drag
    // returns nullptr when there is no active drag or the active drag is not a material payload
    const char* peek_material_drag_path()
    {
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        if (!payload || !payload->IsDataType(ImGuiSp::GDragDropTypes[(int)ImGuiSp::DragPayloadType::Material].data()))
        {
            return nullptr;
        }

        if (payload->DataSize < static_cast<int>(sizeof(ImGuiSp::DragDropPayload)))
        {
            return nullptr;
        }

        const ImGuiSp::DragDropPayload* sp_payload = static_cast<const ImGuiSp::DragDropPayload*>(payload->Data);
        if (sp_payload->path[0] == '\0')
        {
            return nullptr;
        }

        return sp_payload->path;
    }
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
    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 main_viewport_pos = ImGui::GetMainViewport()->Pos;
    const Vector2 offset = Vector2(screen_pos.x - main_viewport_pos.x, screen_pos.y - main_viewport_pos.y);
    Input::SetEditorViewportOffset(offset);

    // publish the viewport's screen-space rect so other systems can snap overlays to it
    m_screen_position = Vector2(screen_pos.x, screen_pos.y);
    m_screen_size     = Vector2(static_cast<float>(width), static_cast<float>(height));

    // draw the image after a potential resolution change call has been made
    ImGuiSp::image(Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output), ImVec2(static_cast<float>(width), static_cast<float>(height)));

    // cache the image rect for hover tests, isitemhovered can return false during drag-drop
    ImVec2 image_rect_min = ImGui::GetItemRectMin();
    ImVec2 image_rect_max = ImGui::GetItemRectMax();

    if (Engine::IsFlagSet(EngineMode::Playing))
    {
        const bool paused = Engine::IsFlagSet(EngineMode::Paused);
        const char* label = paused ? "PAUSED" : "PLAYING";
        const ImVec4 status_color = paused ? ImGui::Style::color_warning : ImGui::Style::color_ok;
        const float dpi = Window::GetDpiScale();
        const ImVec2 text_size = ImGui::CalcTextSize(label);
        const ImVec2 padding = ImVec2(10.0f * dpi, 5.0f * dpi);
        const ImVec2 badge_min = ImVec2(image_rect_min.x + 12.0f * dpi, image_rect_min.y + 12.0f * dpi);
        const ImVec2 badge_max = ImVec2(badge_min.x + text_size.x + padding.x * 2.0f, badge_min.y + text_size.y + padding.y * 2.0f);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(badge_min, badge_max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.02f, 0.02f, 0.02f, 0.82f)), 5.0f * dpi);
        draw_list->AddRect(badge_min, badge_max, ImGui::ColorConvertFloat4ToU32(status_color), 5.0f * dpi);
        draw_list->AddText(ImVec2(badge_min.x + padding.x, badge_min.y + padding.y), ImGui::ColorConvertFloat4ToU32(status_color), label);
    }

    // let the input system know if the mouse is within the viewport
    Input::SetMouseIsInViewport(ImGui::IsItemHovered());

    // material drag-preview, runs before the drop handlers so the drop just clears state without reverting
    {
        const char* drag_path  = peek_material_drag_path();
        bool        drag_active = drag_path != nullptr;
        bool        in_image   = ImGui::IsMouseHoveringRect(image_rect_min, image_rect_max);
        bool        previewing = drag_active && in_image;

        if (previewing)
        {
            Entity*  hovered    = pick_entity_under_cursor();
            uint64_t hovered_id = hovered ? hovered->GetObjectId() : 0;
            if (hovered_id != preview_entity_id)
            {
                revert_material_preview();
                if (hovered)
                {
                    apply_material_preview(hovered, drag_path);
                }
            }
        }
        else if (drag_active)
        {
            // drag still active but cursor moved off the image, restore the original
            revert_material_preview();
        }
        else if (preview_drag_was_active)
        {
            // drag ended this frame, commit the preview by clearing state without reverting
            // the imgui drop handler may not always fire on the release frame due to hover
            // detection edge cases during drag-drop, this preserves the material change so
            // the save reflects what the user saw
            clear_preview_state();
        }

        preview_drag_was_active = drag_active;
    }

    // handle model drop
    if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Model))
    {
        if (payload->path[0] != '\0')
        {
            m_editor->GetWidget<AssetBrowser>()->ShowMeshImportDialog(payload->path);
        }
    }

    // handle prefab drop
    if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Prefab))
    {
        if (payload->path[0] != '\0')
        {
            const char* file_path = payload->path;
            Entity* entity        = World::CreateEntity();
            string name           = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
            entity->SetObjectName(name);
            if (Prefab::LoadFromFile(file_path, entity))
            {
                entity->SetPrefabFilePath(file_path);

                // snapshot the loaded hierarchy as the prefab base so later edits persist as overrides
                entity->MarkPrefabBaseline();
            }
            else
            {
                World::RemoveEntity(entity);
            }
        }
    }

    // handle material drop, the preview already applied the material to the hovered mesh,
    // so commit just means clearing the preview state without restoring the original
    if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Material))
    {
        if (preview_entity_id != 0)
        {
            clear_preview_state();
        }
        else if (payload->path[0] != '\0')
        {
            // fallback for the unlikely case the drop fires without a prior hover frame
            if (Entity* hovered = pick_entity_under_cursor())
            {
                if (Render* render = hovered->GetComponent<Render>())
                {
                    render->SetMaterial(payload->path);
                }
            }
        }
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

        // when ctrl is held, Pick() already handled multi-selection via ToggleSelection(),
        // so we only update the properties panel without overwriting the camera's selection
        if (Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right))
        {
            Properties::Inspect(camera->GetSelectedEntity());
        }
        else
        {
            m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(camera->GetSelectedEntity());
        }
    }

    // Ctrl+D to duplicate selected entities
    if (camera && ImGui::IsWindowFocused() && Input::GetKey(KeyCode::Ctrl_Left) && Input::GetKeyDown(KeyCode::D) && !ImGuiSp::editor_shortcuts_blocked())
    {
        const std::vector<Entity*>& selected_entities = camera->GetSelectedEntities();
        if (!selected_entities.empty())
        {
            // clone all selected entities
            std::vector<Entity*> cloned_entities;
            for (Entity* entity : selected_entities)
            {
                if (entity)
                {
                    Entity* cloned = entity->Clone();
                    if (cloned)
                    {
                        cloned_entities.push_back(cloned);
                    }
                }
            }

            // select the cloned entities instead
            if (!cloned_entities.empty())
            {
                camera->ClearSelection();
                for (Entity* cloned : cloned_entities)
                {
                    camera->AddToSelection(cloned);
                }
                m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(cloned_entities[0]);
            }
        }
    }

    // entity transform gizmo (will only show if entities have been picked)
    if (cvar_transform_handle.GetValueAs<bool>())
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
