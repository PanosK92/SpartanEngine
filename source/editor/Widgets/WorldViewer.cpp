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

//= INCLUDES ============================
#include "pch.h"
#include "WorldViewer.h"
#include "Properties.h"
#include "MenuBar.h"
#include "World/Entity.h"
#include "World/Prefab.h"
#include "World/Components/Light.h"
#include "World/Components/AudioSource.h"
#include "World/Components/Physics.h"
#include "World/Components/Terrain.h"
#include "World/Components/Camera.h"
#include "World/Components/ParticleSystem.h"
#include "Commands/CommandStack.h"
#include "Commands/CommandEntityDelete.h"
#include "Input/Input.h"
#include "../ImGui/ImGui_Extension.h"
SP_WARNINGS_OFF
#include "../ImGui/Source/imgui_stdlib.h"
SP_WARNINGS_ON
//=======================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
//======================

namespace
{
    Entity* entity_clicked = nullptr;
    Entity* entity_hovered = nullptr;
    ImGuiSp::DragDropPayload drag_drop_payload;
    Entity* entity_copied    = nullptr;

    // inline rename state
    uint64_t rename_entity_id     = 0;
    bool     rename_request_focus = false;
    string   rename_buffer;
    ImRect selected_entity_rect;
    uint64_t last_selected_entity_id = 0;
    bool selection_from_click        = false;   // track if selection came from user click (no scroll needed)
    Entity* entity_shift_anchor      = nullptr; // anchor entity for shift-click range selection
    vector<Entity*> entities_in_tree_order;     // cached list of entities in display order

    // reorder drag-drop state
    Entity* reorder_target_entity = nullptr; // entity to insert before/after
    bool reorder_insert_after     = false;   // true = insert after, false = insert before
    float reorder_line_y          = 0.0f;    // y position to draw the insertion line
    float reorder_line_x_min      = 0.0f;    // x start of insertion line
    float reorder_line_x_max      = 0.0f;    // x end of insertion line

    // helper function to collect all active entities in tree display order (depth-first)
    void CollectEntitiesInTreeOrder(Entity* entity, vector<Entity*>& out_entities)
    {
        if (!entity || !entity->GetActive())
        {
            return;
        }

        out_entities.push_back(entity);

        const vector<Entity*>& children = entity->GetChildren();
        for (Entity* child : children)
        {
            CollectEntitiesInTreeOrder(child, out_entities);
        }
    }

    void RefreshEntitiesInTreeOrder()
    {
        entities_in_tree_order.clear();

        static vector<Entity*> root_entities;
        World::GetRootEntities(root_entities);

        for (Entity* entity : root_entities)
        {
            CollectEntitiesInTreeOrder(entity, entities_in_tree_order);
        }
    }

    // select all entities between two entities in tree order (inclusive)
    void SelectEntitiesInRange(Entity* entity_a, Entity* entity_b)
    {
        if (!entity_a || !entity_b)
        {
            return;
        }

        Camera* camera = World::GetCamera();
        if (!camera)
        {
            return;
        }

        RefreshEntitiesInTreeOrder();

        // find indices of both entities
        int index_a = -1;
        int index_b = -1;
        for (int i = 0; i < static_cast<int>(entities_in_tree_order.size()); ++i)
        {
            if (entities_in_tree_order[i]->GetObjectId() == entity_a->GetObjectId())
            {
                index_a = i;
            }
            if (entities_in_tree_order[i]->GetObjectId() == entity_b->GetObjectId())
            {
                index_b = i;
            }
        }

        if (index_a == -1 || index_b == -1)
        {
            return;
        }

        // ensure index_a <= index_b
        if (index_a > index_b)
        {
            std::swap(index_a, index_b);
        }

        // clear current selection and select range
        camera->ClearSelection();
        for (int i = index_a; i <= index_b; ++i)
        {
            camera->AddToSelection(entities_in_tree_order[i]);
        }
    }

    // instantiate a .prefab file as a new entity in the world
    Entity* instantiate_prefab(const std::string& file_path, Entity* parent = nullptr)
    {
        Entity* entity = World::CreateEntity();
        if (!entity)
        {
            return nullptr;
        }

        // use the prefab file name (without extension) as the entity name
        std::string name = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
        entity->SetObjectName(name);

        // load prefab contents (components and children) into the entity
        if (!Prefab::LoadFromFile(file_path, entity))
        {
            SP_LOG_ERROR("Failed to instantiate prefab: %s", file_path.c_str());
            World::RemoveEntity(entity);
            return nullptr;
        }

        // mark the entity as a file prefab so the editor knows about it
        entity->SetPrefabFilePath(file_path);

        // snapshot the loaded hierarchy as the prefab base so later edits persist as overrides
        entity->MarkPrefabBaseline();

        if (parent)
        {
            entity->SetParent(parent);
        }

        return entity;
    }

    void rename_entity_inline(Entity* entity, float width)
    {
        if (rename_request_focus)
        {
            ImGui::SetKeyboardFocusHere();
            rename_request_focus = false;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 1));
        ImGui::SetNextItemWidth(width);

        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
        const bool committed            = ImGui::InputText("##rename_entity_inline", &rename_buffer, flags);
        const bool deactivated          = ImGui::IsItemDeactivated();
        const bool escape_pressed       = ImGui::IsKeyPressed(ImGuiKey_Escape);

        ImGui::PopStyleVar(2);

        auto try_commit = [&]()
        {
            if (!rename_buffer.empty() && rename_buffer != entity->GetObjectName())
            {
                entity->SetObjectName(rename_buffer);
            }
        };

        if (committed)
        {
            try_commit();
            rename_entity_id = 0;
        }
        else if (escape_pressed)
        {
            rename_entity_id = 0;
        }
        else if (deactivated)
        {
            try_commit();
            rename_entity_id = 0;
        }
    }

    Icon component_to_image(Entity* entity)
    {
        IconType type   = IconType::Undefined;
        int match_count = 0;
    
        if (entity->GetComponent<Light>())
        {
            type = IconType::Light;
            ++match_count;
        }
    
        if (entity->GetComponent<Camera>())
        {
            type = IconType::Camera;
            ++match_count;
        }

        if (entity->GetComponent<ParticleSystem>())
        {
            type = IconType::Particle;
            ++match_count;
        }
    
        if (entity->GetComponent<AudioSource>())
        {
            type = IconType::Audio;
            ++match_count;
        }

        if (entity->GetComponent<Terrain>())
        {
            type = IconType::Terrain;
            ++match_count;
        }

        if (entity->GetComponent<Render>())
        {
            type = IconType::Model;
            ++match_count;
        }

        if (match_count > 1)
        {
            return ResourceCache::GetIcon(IconType::Hybrid);
        }
    
        return ResourceCache::GetIcon(match_count == 1 ? type : IconType::Entity);
    }
}

WorldViewer::WorldViewer(Editor* editor) : Widget(editor)
{
    m_title  = "World";
    m_flags |= ImGuiWindowFlags_HorizontalScrollbar;
}

void WorldViewer::OnTickVisible()
{
    TreeShow();

    // on left click, select entity but only on release
    if (ImGui::IsMouseReleased(0))
    {
        // make sure that the mouse was released while on the same entity
        if (Entity* entity_clicked_raw = entity_clicked)
        {
            if (Entity* entity_hovered_raw = entity_hovered)
            {
                if (entity_hovered_raw->GetObjectId() == entity_clicked_raw->GetObjectId())
                {
                    // mark that selection came from user click (no scroll needed)
                    selection_from_click = true;

                    bool ctrl_held  = Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right);
                    bool shift_held = Input::GetKey(KeyCode::Shift_Left) || Input::GetKey(KeyCode::Shift_Right);

                    if (shift_held && entity_shift_anchor)
                    {
                        // shift+click: select range between anchor and clicked entity
                        SelectEntitiesInRange(entity_shift_anchor, entity_clicked_raw);
                    }
                    else if (ctrl_held)
                    {
                        // ctrl+click: toggle selection
                        if (Camera* camera = World::GetCamera())
                        {
                            camera->ToggleSelection(entity_clicked_raw);
                        }
                    }
                    else
                    {
                        SetSelectedEntity(entity_clicked_raw);
                        entity_shift_anchor = entity_clicked_raw;
                    }
                }

                entity_clicked = nullptr;
            }
        }
    }
}

void WorldViewer::TreeShow()
{
    OnTreeBegin();

    // get window rect for window-level drop target (for unparenting)
    ImVec2 window_pos = ImGui::GetWindowPos();
    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
    ImVec2 content_avail = ImGui::GetContentRegionAvail();
    ImRect window_rect = ImRect(
        cursor_pos,
        ImVec2(cursor_pos.x + content_avail.x, window_pos.y + ImGui::GetWindowSize().y)
    );

    bool is_in_game_mode = Engine::IsFlagSet(EngineMode::Playing);
    ImGui::BeginDisabled(is_in_game_mode);
    {
        static vector<Entity*> root_entities;
        World::GetRootEntities(root_entities);

        // iterate over root entities directly, omitting the root node
        for (Entity* entity : root_entities)
        {
            if (entity->GetActive())
            {
                TreeAddEntity(entity);
            }
        }
    }
    ImGui::EndDisabled();

    // window-level drop target for reordering (gaps between entities), unparenting (empty space), or prefab instantiation
    if (ImGui::BeginDragDropTargetCustom(window_rect, ImGui::GetID("##WorldViewerDropTarget")))
    {
        // handle entity reordering/unparenting
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            if (payload->DataSize == sizeof(uint64_t))
            {
                const uint64_t entity_id = *(const uint64_t*)payload->Data;
                if (Entity* dropped_entity = World::GetEntityById(entity_id))
                {
                    if (reorder_target_entity && dropped_entity->GetObjectId() != reorder_target_entity->GetObjectId())
                    {
                        // reorder: move entity to new position
                        Entity* target_parent = reorder_target_entity->GetParent();
                        Entity* dropped_parent = dropped_entity->GetParent();
                        
                        if (target_parent == dropped_parent)
                        {
                            // same parent - just reorder
                            if (target_parent)
                            {
                                std::vector<Entity*>& children = target_parent->GetChildren();
                                uint32_t target_index = 0;
                                for (uint32_t i = 0; i < children.size(); ++i)
                                {
                                    if (children[i] == reorder_target_entity)
                                    {
                                        target_index = reorder_insert_after ? i + 1 : i;
                                        break;
                                    }
                                }
                                target_parent->MoveChildToIndex(dropped_entity, target_index);
                            }
                            else
                            {
                                // root entities - use the target-relative function
                                World::MoveRootEntityNear(dropped_entity, reorder_target_entity, reorder_insert_after);
                            }
                        }
                        else
                        {
                            // different parents - change parent and reorder
                            dropped_entity->SetParent(target_parent);
                            if (target_parent)
                            {
                                std::vector<Entity*>& children = target_parent->GetChildren();
                                uint32_t target_index = 0;
                                for (uint32_t i = 0; i < children.size(); ++i)
                                {
                                    if (children[i] == reorder_target_entity)
                                    {
                                        target_index = reorder_insert_after ? i + 1 : i;
                                        break;
                                    }
                                }
                                target_parent->MoveChildToIndex(dropped_entity, target_index);
                            }
                        }
                    }
                    else
                    {
                        // no reorder target - unparent the entity
                        dropped_entity->SetParent(nullptr);
                    }
                }
            }
        }

        // handle prefab drop from asset browser - instantiate as a root entity
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ImGuiSp::GDragDropTypes[static_cast<int>(ImGuiSp::DragPayloadType::Prefab)].data()))
        {
            if (payload->DataSize >= static_cast<int>(sizeof(ImGuiSp::DragDropPayload)))
            {
                const auto* prefab_payload = static_cast<const ImGuiSp::DragDropPayload*>(payload->Data);
                if (prefab_payload->path[0] != '\0')
                {
                    instantiate_prefab(prefab_payload->path);
                }
            }
        }

        ImGui::EndDragDropTarget();
    }

    // draw reorder insertion line indicator
    if (reorder_target_entity && ImGui::GetDragDropPayload() && ImGui::GetDragDropPayload()->IsDataType("ENTITY"))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 line_color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
        dl->AddLine(ImVec2(reorder_line_x_min, reorder_line_y), ImVec2(reorder_line_x_max, reorder_line_y), line_color, 2.0f);
    }

    OnTreeEnd();
}

void WorldViewer::OnTreeBegin()
{
    entity_hovered = nullptr;
    reorder_target_entity = nullptr;
}

void WorldViewer::OnTreeEnd()
{
    HandleKeyShortcuts();
    HandleClicking();
    Popups();
}

void WorldViewer::TreeAddEntity(Entity* entity)
{
    // early exit if entity is null
    if (!entity)
    {
        return;
    }

    // set up tree node flags - we handle highlighting manually, so no SpanFullWidth or Selected
    ImGuiTreeNodeFlags node_flags            = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_OpenOnArrow;
    const vector<Entity*>& children = entity->GetChildren();
    bool has_children                        = !children.empty();
    if (!has_children)
    {
        node_flags |= ImGuiTreeNodeFlags_Leaf;
    }

    // handle selection (multi-select support)
    Camera* camera = World::GetCamera();
    const bool is_selected  = camera && camera->IsSelected(entity);
    Entity* primary_selected = camera ? camera->GetSelectedEntity() : nullptr;
    const bool first_time_selected = is_selected && primary_selected && primary_selected->GetObjectId() != last_selected_entity_id;

    // auto-expand for selected descendants
    if (primary_selected && primary_selected->IsDescendantOf(entity) && primary_selected->GetObjectId() != last_selected_entity_id)
    {
        ImGui::SetNextItemOpen(true);
    }

    // use draw list channels to draw highlight behind tree node content
    // channel 0 = background (highlight), channel 1 = foreground (tree node, icon, text)
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1); // draw tree node on foreground

    // disable imgui's built-in tree node hover/selection colors - we draw our own
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));

    // start tree node
    const void* node_id     = reinterpret_cast<void*>(static_cast<uint64_t>(entity->GetObjectId()));
    const bool is_node_open = ImGui::TreeNodeEx(node_id, node_flags, "");

    ImGui::PopStyleColor(3);

    // get the full tree node rect (including arrow) for hover detection
    ImVec2 tree_node_min = ImGui::GetItemRectMin();
    ImVec2 tree_node_max = ImGui::GetItemRectMax();

    // scroll to selected entity, but only if selection was programmatic (not from user click)
    if (first_time_selected && primary_selected)
    {
        if (!selection_from_click)
        {
            ImGui::SetScrollHereY(0.25f);
        }
        last_selected_entity_id = primary_selected->GetObjectId();
        selection_from_click    = false; // reset after handling
    }

    // set up row for interaction
    ImGui::SameLine();
    const ImVec2 row_pos   = ImGui::GetCursorScreenPos();
    const float row_height = ImGui::GetTextLineHeightWithSpacing();
    
    // calculate content width (icon + text only)
    const float padding      = ImGui::GetStyle().FramePadding.y * 2.0f;
    const float icon_size    = ImGui::GetTextLineHeightWithSpacing() - padding;
    const float text_width   = ImGui::CalcTextSize(entity->GetObjectName().c_str()).x;
    const float content_width = icon_size + ImGui::GetStyle().ItemSpacing.x + text_width;
    
    // calculate content rect (icon + text area only) for hover detection and highlighting
    ImVec2 content_min = ImVec2(row_pos.x, tree_node_min.y);
    ImVec2 content_max = ImVec2(row_pos.x + content_width, tree_node_min.y + row_height);
    
    // check for reorder position when dragging (top/bottom edge of row)
    bool is_in_reorder_zone = false;
    const ImGuiPayload* active_payload = ImGui::GetDragDropPayload();
    if (active_payload && active_payload->IsDataType("ENTITY"))
    {
        ImVec2 mouse_pos = ImGui::GetMousePos();
        float row_top = tree_node_min.y;
        float row_bottom = tree_node_min.y + row_height;
        float reorder_zone = row_height * 0.35f; // top/bottom 35% of row for reordering
        
        // check if mouse is in this row's vertical range
        if (mouse_pos.y >= row_top && mouse_pos.y <= row_bottom)
        {
            // check if mouse is in top zone (insert before) or bottom zone (insert after)
            if (mouse_pos.y < row_top + reorder_zone)
            {
                reorder_target_entity = entity;
                reorder_insert_after = false;
                reorder_line_y = row_top;
                reorder_line_x_min = tree_node_min.x;
                reorder_line_x_max = tree_node_min.x + ImGui::GetContentRegionAvail().x + (row_pos.x - tree_node_min.x);
                is_in_reorder_zone = true;
            }
            else if (mouse_pos.y > row_bottom - reorder_zone)
            {
                reorder_target_entity = entity;
                reorder_insert_after = true;
                reorder_line_y = row_bottom;
                reorder_line_x_min = tree_node_min.x;
                reorder_line_x_max = tree_node_min.x + ImGui::GetContentRegionAvail().x + (row_pos.x - tree_node_min.x);
                is_in_reorder_zone = true;
            }
        }
    }

    // check hover on content area only (but not when in reorder zone during drag)
    bool is_row_hovered = ImGui::IsMouseHoveringRect(content_min, content_max, true);
    if (is_row_hovered && !is_in_reorder_zone)
    {
        entity_hovered = entity;
    }
    
    // draw selection or hover highlight on background channel (content area only)
    // don't draw hover highlight when in reorder zone - only show the line
    bool show_hover_highlight = is_row_hovered && !is_in_reorder_zone;
    if (is_selected || show_hover_highlight)
    {
        dl->ChannelsSetCurrent(0); // background channel
        ImU32 highlight_color = is_selected ? ImGui::GetColorU32(ImGuiCol_Header) : ImGui::GetColorU32(ImGuiCol_HeaderHovered);
        dl->AddRectFilled(content_min, content_max, highlight_color);
        dl->ChannelsSetCurrent(1); // back to foreground
    }

    // handle clicking and drag-and-drop
    // use reduced height to leave gaps for reorder drop zones (handled by window-level target)
    ImGui::PushID(node_id);
    const float reorder_gap = row_height * 0.3f; // 30% gap at top/bottom for reordering
    const float button_height = row_height - reorder_gap;
    const float button_y_offset = reorder_gap * 0.5f;
    
    // add vertical offset for the button
    ImGui::SetCursorScreenPos(ImVec2(row_pos.x, row_pos.y + button_y_offset));
    ImGui::InvisibleButton("row_btn", ImVec2(content_width, button_height));

    // drag source
    if (!Engine::IsFlagSet(EngineMode::Playing))
    {
        if (ImGui::BeginDragDropSource())
        {
            uint64_t entity_id = entity->GetObjectId();
            ImGui::SetDragDropPayload("ENTITY", &entity_id, sizeof(uint64_t));
            ImGui::Text("%s", entity->GetObjectName().c_str());
            ImGui::EndDragDropSource();
        }
    }

    // drop target - handles parenting (middle zone) and prefab instantiation
    // reordering is handled by window-level drop target in the gaps
    if (ImGui::BeginDragDropTarget())
    {
        // entity parenting
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            if (payload->DataSize != sizeof(uint64_t))
            {
                SP_LOG_ERROR("Invalid drag-and-drop payload size for entity.");
            }
            else
            {
                const uint64_t entity_id = *(const uint64_t*)payload->Data;
                if (Entity* dropped_entity = World::GetEntityById(entity_id))
                {
                    if (dropped_entity->GetObjectId() != entity->GetObjectId())
                    {
                        // parent to this entity
                        if (entity->GetParent() == dropped_entity)
                        {
                            entity->SetParent(nullptr);
                            dropped_entity->SetParent(entity);
                        }
                        else if (entity->IsDescendantOf(dropped_entity))
                        {
                            Entity* old_parent = entity->GetParent();
                            if (!old_parent || old_parent != dropped_entity)
                            {
                                entity->SetParent(dropped_entity);
                            }
                            else
                            {
                                SP_LOG_WARNING("cannot make %s a child of %s due to circular parenting.", entity->GetObjectName().c_str(), dropped_entity->GetObjectName().c_str());
                            }
                        }
                        else
                        {
                            dropped_entity->SetParent(entity);
                        }
                    }
                }
                else
                {
                    SP_LOG_WARNING("Dropped entity with ID %llu not found.", entity_id);
                }
            }
        }

        // prefab drop from asset browser - instantiate as a child of the target entity
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ImGuiSp::GDragDropTypes[static_cast<int>(ImGuiSp::DragPayloadType::Prefab)].data()))
        {
            if (payload->DataSize >= static_cast<int>(sizeof(ImGuiSp::DragDropPayload)))
            {
                const auto* prefab_payload = static_cast<const ImGuiSp::DragDropPayload*>(payload->Data);
                if (prefab_payload->path[0] != '\0')
                {
                    instantiate_prefab(prefab_payload->path, entity);
                }
            }
        }

        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();

    // draw icon (still on foreground channel)
    ImVec2 icon_pos        = row_pos;
    const Icon entry       = component_to_image(entity);
    float next_x           = icon_pos.x;
    if (entry.texture)
    {
        const float padding   = ImGui::GetStyle().FramePadding.y * 2.0f;
        const float icon_size = ImGui::GetTextLineHeightWithSpacing() - padding;
        const float y_offset  = (ImGui::GetTextLineHeightWithSpacing() - icon_size) * 0.25f;
        ImVec2 icon_min       = ImVec2(icon_pos.x, icon_pos.y + y_offset);
        ImVec2 icon_max       = ImVec2(icon_min.x + icon_size, icon_min.y + icon_size);
        dl->AddImage(reinterpret_cast<ImTextureID>(entry.texture), icon_min, icon_max,
            ImVec2(entry.uv_min.x, entry.uv_min.y), ImVec2(entry.uv_max.x, entry.uv_max.y));
        next_x                = icon_max.x + ImGui::GetStyle().ItemSpacing.x;
    }

    // draw text (still on foreground channel) skipped while inline renaming this entity
    const bool is_renaming_this = rename_entity_id == entity->GetObjectId();
    const ImVec2 text_pos       = ImVec2(next_x, row_pos.y - (ImGui::GetTextLineHeightWithSpacing() - ImGui::GetTextLineHeight()) * 0.25f);
    if (!is_renaming_this)
    {
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_pos, ImGui::GetColorU32(ImGuiCol_Text), entity->GetObjectName().c_str());
    }

    // merge channels before processing children (they will have their own channel splits)
    dl->ChannelsMerge();

    // inline rename input drawn on top of where the label would go
    if (is_renaming_this)
    {
        ImGui::SetCursorScreenPos(ImVec2(next_x, row_pos.y));
        const float available_width = ImGui::GetContentRegionAvail().x;
        rename_entity_inline(entity, available_width);
    }

    // note: selection is handled in OnTickVisible on mouse release to avoid double-selection

    // recursively add children
    if (is_node_open)
    {
        if (has_children)
        {
            for (const auto& child : children)
            {
                TreeAddEntity(World::GetEntityById(child->GetObjectId()));
            }
        }
        ImGui::TreePop();
    }
}

void WorldViewer::HandleClicking()
{
    const auto is_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const auto left_click        = ImGui::IsMouseClicked(0);
    const auto right_click       = ImGui::IsMouseClicked(1);
    const auto double_click      = ImGui::IsMouseDoubleClicked(0);

    // since we are handling clicking manually, we must ensure we are inside the window
    if (!is_window_hovered)
    {
        return;
    }

    // double-click on item - Focus camera on entity
    if (double_click && entity_hovered)
    {
        selection_from_click = true;
        SetSelectedEntity(entity_hovered);
        entity_shift_anchor = entity_hovered;
        if (Camera* camera = World::GetCamera())
        {
            camera->FocusOnSelectedEntity();
        }
        return; // don't process as regular click
    }

    // left click on item - Don't select yet
    if (left_click && entity_hovered)
    {
        entity_clicked = entity_hovered;
    }

    // right click on item - Select and show context menu
    if (ImGui::IsMouseClicked(1))
    {
        if (entity_hovered)
        {
            // if entity is not already selected, select it (replacing current selection)
            // if already selected, keep the current selection for multi-entity context menu
            if (Camera* camera = World::GetCamera())
            {
                if (!camera->IsSelected(entity_hovered))
                {
                    selection_from_click = true;
                    SetSelectedEntity(entity_hovered);
                    entity_shift_anchor = entity_hovered;
                }
            }
        }

        ImGui::OpenPopup("##HierarchyContextMenu");
    }

    // clicking on empty space - Clear selection (only if Ctrl not held)
    if ((left_click || right_click) && !entity_hovered)
    {
        bool ctrl_held = Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right);
        if (!ctrl_held)
        {
            SetSelectedEntity(nullptr);
            entity_shift_anchor = nullptr;
        }
    }
}

void WorldViewer::SetSelectedEntity(Entity* entity)
{
    // while in game mode the tree is not interactive, so don't allow selection
    bool is_in_game_mode = Engine::IsFlagSet(EngineMode::Playing);
    if (is_in_game_mode)
    {
        return;
    }

    if (Camera* camera = World::GetCamera())
    {
        camera->SetSelectedEntity(entity);
    }

    Properties::Inspect(entity);
}

void WorldViewer::Popups()
{
    PopupContextMenu();
}

void WorldViewer::PopupContextMenu() const
{
    if (!ImGui::BeginPopup("##HierarchyContextMenu"))
    {
        return;
    }

    // get selected entities
    Camera* camera = World::GetCamera();
    Entity* selected_entity = camera ? camera->GetSelectedEntity() : nullptr;
    uint32_t selected_count = camera ? camera->GetSelectedEntityCount() : 0;

    const bool on_entity = selected_entity != nullptr;
    const bool multiple_selected = selected_count > 1;

    // show selection count if multiple
    if (multiple_selected)
    {
        ImGui::Text("%d entities selected", selected_count);
        ImGui::Separator();
    }

    if (ImGui::MenuItem("Copy") && on_entity && !multiple_selected)
    {
        entity_copied = selected_entity;
    }

    if (ImGui::MenuItem("Paste") && entity_copied)
    {
        Entity* cloned = entity_copied->Clone();
        if (cloned)
        {
            cloned->SetParent(entity_copied->GetParent());

            // find next available copy number
            const std::string& base_name = entity_copied->GetObjectName();
            uint32_t copy_number = 1;
            bool found = true;
            while (found)
            {
                found = false;
                std::string test_name = base_name + "_" + std::to_string(copy_number);
                for (Entity* entity : World::GetEntities())
                {
                    if (entity->GetObjectName() == test_name)
                    {
                        found = true;
                        copy_number++;
                        break;
                    }
                }
            }
            cloned->SetObjectName(base_name + "_" + std::to_string(copy_number));
        }
    }

    if (ImGui::MenuItem("Rename") && on_entity && !multiple_selected)
    {
        rename_entity_id     = selected_entity->GetObjectId();
        rename_request_focus = true;
        rename_buffer        = selected_entity->GetObjectName();
    }

    if (ImGui::MenuItem("Focus") && on_entity)
    {
        World::GetCamera()->FocusOnSelectedEntity();
    }

    // delete shows count if multiple selected
    std::string delete_label = multiple_selected ? "Delete (" + std::to_string(selected_count) + ")" : "Delete";
    if (ImGui::MenuItem(delete_label.c_str(), "Delete") && on_entity)
    {
        if (multiple_selected)
        {
            // delete all selected entities
            std::vector<Entity*> to_delete = camera->GetSelectedEntities();
            for (Entity* entity : to_delete)
            {
                if (entity)
                {
                    ActionEntityDelete(entity);
                }
            }
            camera->ClearSelection();
        }
        else
        {
            ActionEntityDelete(selected_entity);
        }
    }
    ImGui::Separator();

    // EMPTY
    if (ImGui::MenuItem("Create Empty"))
    {
        ActionEntityCreateEmpty();
    }

    // 3D OBJECTS
    if (ImGui::BeginMenu("3D Objects"))
    {
        if (ImGui::MenuItem("Cube"))
        {
            ActionEntityCreateCube();
        }
        else if (ImGui::MenuItem("Quad"))
        {
            ActionEntityCreateQuad();
        }
        else if (ImGui::MenuItem("Sphere"))
        {
            ActionEntityCreateSphere();
        }
        else if (ImGui::MenuItem("Cylinder"))
        {
            ActionEntityCreateCylinder();
        }
        else if (ImGui::MenuItem("Cone"))
        {
            ActionEntityCreateCone();
        }

        ImGui::EndMenu();
    }

    // CAMERA
    if (ImGui::MenuItem("Camera"))
    {
        ActionEntityCreateCamera();
    }

    // LIGHT
    if (ImGui::BeginMenu("Light"))
    {
        if (ImGui::MenuItem("Directional"))
        {
            ActionEntityCreateLightDirectional();
        }
        else if (ImGui::MenuItem("Point"))
        {
            ActionEntityCreateLightPoint();
        }
        else if (ImGui::MenuItem("Spot"))
        {
            ActionEntityCreateLightSpot();
        }
        else if (ImGui::MenuItem("Area"))
        {
            ActionEntityCreateLightArea();
        }

        ImGui::EndMenu();
    }

    // PHYSICS
    if (ImGui::BeginMenu("Physics"))
    {
        if (ImGui::MenuItem("Physics Body"))
        {
            ActionEntityCreatePhysicsBody();
        }

        ImGui::EndMenu();
    }

    // AUDIO
    if (ImGui::BeginMenu("Audio"))
    {
        if (ImGui::MenuItem("Audio Source"))
        {
            ActionEntityCreateAudioSource();
        }

        ImGui::EndMenu();
    }

    // TERRAIN
    if (ImGui::MenuItem("Terrain"))
    {
        ActionEntityCreateTerrain();
    }

    ImGui::EndPopup();
}

void WorldViewer::HandleKeyShortcuts()
{
    // skip engine shortcuts while inline rename input or any other text field is active
    if (rename_entity_id != 0 || ImGui::GetIO().WantTextInput)
    {
        return;
    }

    // Delete - deletes all selected entities
    if (Input::GetKey(KeyCode::Delete))
    {
        if (Camera* camera = World::GetCamera())
        {
            // copy the vector since we're modifying it
            std::vector<Entity*> to_delete = camera->GetSelectedEntities();
            for (Entity* entity : to_delete)
            {
                if (entity)
                {
                    ActionEntityDelete(entity);
                }
            }
            camera->ClearSelection();
        }
    }

    // Save: Ctrl + S
    if (Input::GetKey(KeyCode::Ctrl_Left) && Input::GetKeyDown(KeyCode::S))
    {
        const string& file_path = World::GetFilePath();

        if (file_path.empty())
        {
            m_editor->GetWidget<MenuBar>()->ShowWorldSaveDialog();
        }
        else
        {
            World::SaveToFile(file_path);
        }
    }

    // Load: Ctrl + L
    if (Input::GetKey(KeyCode::Ctrl_Left) && Input::GetKeyDown(KeyCode::L))
    {
        m_editor->GetWidget<MenuBar>()->ShowWorldLoadDialog();
    }

    // Undo and Redo: Ctrl + Z, Ctrl + Shift + Z
    if (Input::GetKey(KeyCode::Ctrl_Left) && Input::GetKeyDown(KeyCode::Z))
    {
        if (Input::GetKey(KeyCode::Shift_Left))
        {
            CommandStack::Redo();
        }
        else
        {
            CommandStack::Undo();
        }
    }

    // Copy: Ctrl + C
    if (Input::GetKey(KeyCode::Ctrl_Left) && Input::GetKeyDown(KeyCode::C))
    {
        if (Camera* camera = World::GetCamera())
        {
            if (Entity* selected_entity = camera->GetSelectedEntity())
            {
                entity_copied = selected_entity;
            }
        }
    }

    // Paste: Ctrl + V
    if (Input::GetKey(KeyCode::Ctrl_Left) && Input::GetKeyDown(KeyCode::V))
    {
        if (entity_copied)
        {
            Entity* cloned = entity_copied->Clone();
            if (cloned)
            {
                cloned->SetParent(entity_copied->GetParent());

                // find next available copy number
                const std::string& base_name = entity_copied->GetObjectName();
                uint32_t copy_number = 1;
                bool found = true;
                while (found)
                {
                    found = false;
                    std::string test_name = base_name + "_" + std::to_string(copy_number);
                    for (Entity* entity : World::GetEntities())
                    {
                        if (entity->GetObjectName() == test_name)
                        {
                            found = true;
                            copy_number++;
                            break;
                        }
                    }
                }
                cloned->SetObjectName(base_name + "_" + std::to_string(copy_number));
            }
        }
    }
}

void WorldViewer::ActionEntityDelete(Entity* entity)
{
    SP_ASSERT_MSG(entity != nullptr, "Entity is null");

    // check if entity still exists (might have been deleted as a child of another entity)
    if (!World::EntityExists(entity))
    {
        return;
    }

    // create undo command (stores entity state before deletion)
    auto command = std::make_shared<CommandEntityDelete>(entity);

    // delete the entity
    command->OnApply();

    // push to undo stack
    CommandStack::Push(command);
}

Entity* WorldViewer::ActionEntityCreateEmpty()
{
    Entity* entity = World::CreateEntity();
    
    if (Camera* camera = World::GetCamera())
    {
        if (Entity* selected_entity = camera->GetSelectedEntity())
        {
            entity->SetParent(selected_entity);
        }
    }

    return entity;
}

void WorldViewer::ActionEntityCreateCube()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Render>();
    renderable->SetMesh(MeshType::Cube);
    renderable->SetDefaultMaterial();
    entity->AddComponent<Physics>()->SetBodyType(BodyType::Box);
    entity->SetObjectName("Cube");
}

void WorldViewer::ActionEntityCreateQuad()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Render>();
    renderable->SetMesh(MeshType::Quad);
    renderable->SetDefaultMaterial();
    entity->AddComponent<Physics>()->SetBodyType(BodyType::Plane);
    entity->SetObjectName("Quad");
}

void WorldViewer::ActionEntityCreateSphere()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Render>();
    renderable->SetMesh(MeshType::Sphere);
    renderable->SetDefaultMaterial();
    entity->AddComponent<Physics>()->SetBodyType(BodyType::Sphere);
    entity->SetObjectName("Sphere");
}

void WorldViewer::ActionEntityCreateCylinder()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Render>();
    renderable->SetMesh(MeshType::Cylinder);
    renderable->SetDefaultMaterial();
    entity->AddComponent<Physics>()->SetBodyType(BodyType::Capsule);
    entity->SetObjectName("Cylinder");
}

void WorldViewer::ActionEntityCreateCone()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Render>();
    renderable->SetMesh(MeshType::Cone);
    renderable->SetDefaultMaterial();
    entity->AddComponent<Physics>()->SetBodyType(BodyType::Mesh);
    entity->SetObjectName("Cone");
}

void WorldViewer::ActionEntityCreateCamera()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Camera>();
    entity->SetObjectName("Camera");
    entity->SetPosition(math::Vector3(0.0f, 3.0f, -5.0f));
}

void WorldViewer::ActionEntityCreateTerrain()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Terrain>();
    entity->SetObjectName("Terrain");
}

void WorldViewer::ActionEntityCreateLightDirectional()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Light>()->SetLightType(LightType::Directional);
    entity->SetObjectName("Directional");
}

void WorldViewer::ActionEntityCreateLightPoint()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetObjectName("Point");

    Light* light = entity->AddComponent<Light>();
    light->SetLightType(LightType::Point);
    light->SetIntensity(LightIntensity::bulb_150_watt);
}

void WorldViewer::ActionEntityCreateLightSpot()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetObjectName("Spot");

    Light* light = entity->AddComponent<Light>();
    light->SetLightType(LightType::Spot);
    light->SetIntensity(LightIntensity::bulb_150_watt);
}

void WorldViewer::ActionEntityCreateLightArea()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetObjectName("Area");

    Light* light = entity->AddComponent<Light>();
    light->SetLightType(LightType::Area);
    light->SetIntensity(LightIntensity::bulb_150_watt);
}

void WorldViewer::ActionEntityCreatePhysicsBody()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Physics>();
    entity->SetObjectName("Physics");
}

void WorldViewer::ActionEntityCreateAudioSource()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<AudioSource>();
    entity->SetObjectName("Physics");
}
