/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =============================
#include "pch.h"
#include "../EditorHistory.h"
#include "Viewport.h"
#include "AssetBrowser.h"
#include "WorldViewer.h"
#include "Properties.h"
#include "TerrainEditor.h"
#include "rhi/RHI_Device.h"
#include "rendering/Renderer.h"
#include "rendering/Material.h"
#include "resource/ResourceCache.h"
#include "world/Prefab.h"
#include "world/components/Render.h"
#include "world/components/Camera.h"
#include "math/Ray.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_Style.h"
#include "../imgui/ImGui_TransformGizmo.h"
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

    // drag preview state, the entity is tracked by id so a deletion mid-drag cannot dangle on revert
    uint64_t             preview_entity_id            = 0;
    shared_ptr<Material> preview_original_material;
    bool                 preview_original_was_default = false;
    bool                 preview_drag_was_active      = false;

    // triangle precision picking, aabbs alone fail because a gltf scene wrapper swallows the whole world
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
    m_dock          = WidgetDock::Center;
    m_size_initial  = Vector2(400, 250);
    m_flags        |= ImGuiWindowFlags_NoScrollbar;
    m_padding       = Vector2(0.0f);
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

    // Draw without submitting another item: picking and drops below belong to the viewport image.
    if (!World::GetCamera())
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(image_rect_min, image_rect_max, ImGui::EditorUi::color(ImGui::Style::color_void));
        const ImVec2 center((image_rect_min.x + image_rect_max.x) * 0.5f, (image_rect_min.y + image_rect_max.y) * 0.5f);
        const float title_size = ImGui::EditorUi::scaled(34.0f);
        const float tracking   = title_size * 0.55f;
        ImFont* font           = Editor::font_bold ? Editor::font_bold : ImGui::GetFont();
        const float title_w    = ImGui::EditorUi::calc_text_tracked("SPARTAN", tracking, font, title_size);
        const char* hint       = "no world loaded  /  open a world to begin";
        const float hint_w     = ImGui::EditorUi::micro_label_width(hint, Editor::font_mono_medium);
        if (image_rect_max.x - image_rect_min.x > ImMax(title_w, hint_w) + ImGui::EditorUi::scaled(48.0f))
        {
            const float rule_y  = IM_ROUND(center.y + ImGui::EditorUi::scaled(10.0f));
            const float rule_w  = title_w * 0.5f;
            const ImU32 clear   = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_accent_1, 0.0f));
            const ImU32 lit     = ImGui::EditorUi::color(ImGui::Style::color_accent_1);
            ImGui::EditorUi::draw_text_tracked(draw, ImVec2(IM_ROUND(center.x - title_w * 0.5f), IM_ROUND(center.y - title_size - ImGui::EditorUi::scaled(4.0f))), ImGui::EditorUi::color(ImGui::Style::color_text), "SPARTAN", tracking, font, title_size);
            draw->AddRectFilledMultiColor(ImVec2(center.x - rule_w, rule_y), ImVec2(center.x, rule_y + 1.0f), clear, lit, lit, clear);
            draw->AddRectFilledMultiColor(ImVec2(center.x, rule_y), ImVec2(center.x + rule_w, rule_y + 1.0f), lit, clear, clear, lit);
            ImGui::EditorUi::draw_micro_label(draw, ImVec2(IM_ROUND(center.x - hint_w * 0.5f), rule_y + ImGui::EditorUi::scaled(12.0f)), ImGui::GetTextLineHeight(), hint, ImGui::Style::color_text_muted, Editor::font_mono_medium);
        }
    }

    if (Engine::IsFlagSet(EngineMode::Playing))
    {
        // a heads up display: the frame edges glow inward with the signal, a status readout floats in the top left, clear of the performance overlay
        const bool paused     = Engine::IsFlagSet(EngineMode::Paused);
        const ImVec4 signal   = paused ? ImGui::Style::color_warning : ImGui::Style::color_accent_1;
        const float inset     = ImGui::EditorUi::scaled(14.0f);
        const ImVec2 hud_min  = ImVec2(image_rect_min.x + inset, image_rect_min.y + inset);
        const ImVec2 hud_max  = ImVec2(image_rect_max.x - inset, image_rect_max.y - inset);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        {
            const float breathe = 0.85f + 0.15f * sinf(static_cast<float>(ImGui::GetTime()) * 2.2f);
            const float depth   = ImGui::EditorUi::scaled(44.0f);
            const ImU32 lit     = ImGui::EditorUi::color(ImGui::EditorUi::alpha(signal, 0.20f * breathe));
            const ImU32 clear   = ImGui::EditorUi::color(ImGui::EditorUi::alpha(signal, 0.0f));
            const ImVec2 a      = image_rect_min;
            const ImVec2 b      = image_rect_max;
            draw_list->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + depth), lit, lit, clear, clear);
            draw_list->AddRectFilledMultiColor(ImVec2(a.x, b.y - depth), b, clear, clear, lit, lit);
            draw_list->AddRectFilledMultiColor(a, ImVec2(a.x + depth, b.y), lit, clear, clear, lit);
            draw_list->AddRectFilledMultiColor(ImVec2(b.x - depth, a.y), b, clear, lit, lit, clear);
        }

        const char* label      = paused ? "paused" : "live";
        const float pill_h     = ImGui::EditorUi::scaled(20.0f);
        const float pad        = ImGui::EditorUi::scaled(9.0f);
        const float dot_r      = ImGui::EditorUi::scaled(3.0f);
        const float label_w    = ImGui::EditorUi::micro_label_width(label, Editor::font_mono_medium);
        const float pill_w     = pad * 2.0f + dot_r * 2.0f + ImGui::EditorUi::scaled(8.0f) + label_w;
        const ImVec2 pill_min  = ImVec2(IM_ROUND(hud_min.x), IM_ROUND(hud_min.y));
        const ImVec2 pill_max  = ImVec2(pill_min.x + pill_w, pill_min.y + pill_h);
        ImGui::EditorUi::draw_glow(draw_list, pill_min, pill_max, signal, pill_h * 0.5f, ImGui::EditorUi::scaled(10.0f), 0.8f);
        draw_list->AddRectFilled(pill_min, pill_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_void, 0.80f)), pill_h * 0.5f);
        ImGui::EditorUi::draw_lit_rim(draw_list, pill_min, pill_max, pill_h * 0.5f, ImGui::EditorUi::alpha(signal, 0.9f), ImGui::EditorUi::alpha(signal, 0.25f), pill_h);
        ImGui::EditorUi::status_dot(draw_list, ImVec2(pill_min.x + pad + dot_r, (pill_min.y + pill_max.y) * 0.5f), dot_r, signal, !paused);
        ImGui::EditorUi::draw_micro_label(draw_list, ImVec2(pill_min.x + pad + dot_r * 2.0f + ImGui::EditorUi::scaled(8.0f), pill_min.y), pill_h, label, signal, Editor::font_mono_medium);
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
            // the imgui drop handler can miss the release frame, so commit here instead of reverting
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
                editor_history::Created(entity);
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
            if (auto entity = World::GetEntityById(preview_entity_id))
            {
                auto render = entity->GetComponent<Render>();
                if (render && render->GetMaterial())
                {
                    auto material = std::static_pointer_cast<Material>(render->GetMaterial()->shared_from_this());
                    revert_material_preview();
                    editor_history::EntityScope history(entity, true);
                    render->SetMaterial(material);
                }
            }
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
    if (camera && ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered() && ImGui::TransformGizmo::allow_picking() && !TerrainEditor::IsSculptActive())
    {
        camera->Pick();
        m_editor->GetWidget<WorldViewer>()->SetSelectedEntity(camera->GetSelectedEntity());
        if (camera->GetSelectedEntity())
        {
            camera->FocusOnSelectedEntity();
        }
    }
    // mouse picking (with multi-select via Ctrl handled in Pick())
    else if (camera && ImGui::IsMouseClicked(0) && ImGui::IsItemHovered() && ImGui::TransformGizmo::allow_picking() && !TerrainEditor::IsSculptActive())
    {
        camera->Pick();

        // when ctrl is held, Pick() already handled multi-selection via ToggleSelection(),
        // so we only update the properties panel without overwriting the camera's selection
        if (Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right))
        {
            Properties::ClearMaterialInspection();
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
                    bool selected_ancestor = false;
                    for (auto parent = entity->GetParent(); parent; parent = parent->GetParent())
                        if (std::find(selected_entities.begin(), selected_entities.end(), parent) != selected_entities.end()) { selected_ancestor = true; break; }
                    if (selected_ancestor) continue;
                    Entity* cloned = entity->Clone();
                    if (cloned)
                    {
                        cloned_entities.push_back(cloned);
                    }
                }
            }

            editor_history::Created(cloned_entities);
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

    // The gizmo projects its own pivot, which can be the geometry center of a hierarchy.
    // Culling by the selected entity's origin hides visible gizmos on offset geometry.
    ImGui::TransformGizmo::tick();

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
