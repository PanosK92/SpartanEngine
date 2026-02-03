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
#include "../MenuBar.h"
#include "World/Entity.h"
#include "World/Components/Light.h"
#include "World/Components/AudioSource.h"
#include "World/Components/Physics.h"
#include "World/Components/Terrain.h"
#include "World/Components/Camera.h"
#include "Commands/CommandStack.h"
#include "Commands/CommandEntityDelete.h"
#include "Input/Input.h"
#include "../ImGui/ImGui_Extension.h"
SP_WARNINGS_OFF
#include "../ImGui/Source/imgui_stdlib.h"
SP_WARNINGS_ON
//=======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    spartan::Entity* entity_clicked = nullptr;
    spartan::Entity* entity_hovered = nullptr;
    ImGuiSp::DragDropPayload drag_drop_payload;
    bool popup_rename_entity       = false;
    spartan::Entity* entity_copied = nullptr;
    ImRect selected_entity_rect;
    uint64_t last_selected_entity_id = 0;
    bool selection_from_click        = false; // track if selection came from user click (no scroll needed)
    spartan::Entity* entity_shift_anchor = nullptr; // anchor entity for shift-click range selection
    vector<spartan::Entity*> entities_in_tree_order; // cached list of entities in display order

    // reorder drag-drop state
    spartan::Entity* reorder_target_entity = nullptr; // entity to insert before/after
    bool reorder_insert_after              = false;   // true = insert after, false = insert before
    float reorder_line_y                   = 0.0f;    // y position to draw the insertion line
    float reorder_line_x_min               = 0.0f;    // x start of insertion line
    float reorder_line_x_max               = 0.0f;    // x end of insertion line

    // helper function to collect all active entities in tree display order (depth-first)
    void CollectEntitiesInTreeOrder(spartan::Entity* entity, vector<spartan::Entity*>& out_entities)
    {
        if (!entity || !entity->GetActive())
            return;

        out_entities.push_back(entity);

        const vector<spartan::Entity*>& children = entity->GetChildren();
        for (spartan::Entity* child : children)
        {
            CollectEntitiesInTreeOrder(child, out_entities);
        }
    }

    void RefreshEntitiesInTreeOrder()
    {
        entities_in_tree_order.clear();

        static vector<spartan::Entity*> root_entities;
        spartan::World::GetRootEntities(root_entities);

        for (spartan::Entity* entity : root_entities)
        {
            CollectEntitiesInTreeOrder(entity, entities_in_tree_order);
        }
    }

    // select all entities between two entities in tree order (inclusive)
    void SelectEntitiesInRange(spartan::Entity* entity_a, spartan::Entity* entity_b)
    {
        if (!entity_a || !entity_b)
            return;

        spartan::Camera* camera = spartan::World::GetCamera();
        if (!camera)
            return;

        RefreshEntitiesInTreeOrder();

        // find indices of both entities
        int index_a = -1;
        int index_b = -1;
        for (int i = 0; i < static_cast<int>(entities_in_tree_order.size()); ++i)
        {
            if (entities_in_tree_order[i]->GetObjectId() == entity_a->GetObjectId())
                index_a = i;
            if (entities_in_tree_order[i]->GetObjectId() == entity_b->GetObjectId())
                index_b = i;
        }

        if (index_a == -1 || index_b == -1)
            return;

        // ensure index_a <= index_b
        if (index_a > index_b)
            std::swap(index_a, index_b);

        // clear current selection and select range
        camera->ClearSelection();
        for (int i = index_a; i <= index_b; ++i)
        {
            camera->AddToSelection(entities_in_tree_order[i]);
        }
    }

    spartan::RHI_Texture* component_to_image(spartan::Entity* entity)
    {
        spartan::RHI_Texture* icon = nullptr;
        int match_count = 0;
    
        if (entity->GetComponent<spartan::Light>())
        {
            icon = spartan::ResourceCache::GetIcon(spartan::IconType::Light);
            ++match_count;
        }
    
        if (entity->GetComponent<spartan::Camera>())
        {
            icon = spartan::ResourceCache::GetIcon(spartan::IconType::Camera);
            ++match_count;
        }
    
        if (entity->GetComponent<spartan::AudioSource>())
        {
            icon = spartan::ResourceCache::GetIcon(spartan::IconType::Audio);
            ++match_count;
        }

        // everything has physics, ignore it
        //if (entity->GetComponent<spartan::Physics>())
        //{
        //    icon = spartan::ResourceCache::GetIcon(spartan::IconType::Physics);
        //    ++match_count;
        //}
    
        if (entity->GetComponent<spartan::Terrain>())
        {
            icon = spartan::ResourceCache::GetIcon(spartan::IconType::Terrain);
            ++match_count;
        }

        if (entity->GetComponent<spartan::Renderable>())
        {
            icon = spartan::ResourceCache::GetIcon(spartan::IconType::Model);
            ++match_count;
        }

        if (match_count > 1)
            return spartan::ResourceCache::GetIcon(spartan::IconType::Hybrid);
    
        return icon ? icon : spartan::ResourceCache::GetIcon(spartan::IconType::Entity);
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
        if (spartan::Entity* entity_clicked_raw = entity_clicked)
        {
            if (spartan::Entity* entity_hovered_raw = entity_hovered)
            {
                if (entity_hovered_raw->GetObjectId() == entity_clicked_raw->GetObjectId())
                {
                    // mark that selection came from user click (no scroll needed)
                    selection_from_click = true;

                    bool ctrl_held  = spartan::Input::GetKey(spartan::KeyCode::Ctrl_Left) || spartan::Input::GetKey(spartan::KeyCode::Ctrl_Right);
                    bool shift_held = spartan::Input::GetKey(spartan::KeyCode::Shift_Left) || spartan::Input::GetKey(spartan::KeyCode::Shift_Right);

                    if (shift_held && entity_shift_anchor)
                    {
                        // shift+click: select range between anchor and clicked entity
                        SelectEntitiesInRange(entity_shift_anchor, entity_clicked_raw);
                    }
                    else if (ctrl_held)
                    {
                        // ctrl+click: toggle selection
                        if (spartan::Camera* camera = spartan::World::GetCamera())
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

    bool is_in_game_mode = spartan::Engine::IsFlagSet(spartan::EngineMode::Playing);
    ImGui::BeginDisabled(is_in_game_mode);
    {
        static vector<spartan::Entity*> root_entities;
        spartan::World::GetRootEntities(root_entities);

        // iterate over root entities directly, omitting the root node
        for (spartan::Entity* entity : root_entities)
        {
            if (entity->GetActive())
            {
                TreeAddEntity(entity);
            }
        }
    }
    ImGui::EndDisabled();

    // window-level drop target for reordering (gaps between entities) or unparenting (empty space)
    if (ImGui::BeginDragDropTargetCustom(window_rect, ImGui::GetID("##WorldViewerDropTarget")))
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            if (payload->DataSize == sizeof(uint64_t))
            {
                const uint64_t entity_id = *(const uint64_t*)payload->Data;
                if (spartan::Entity* dropped_entity = spartan::World::GetEntityById(entity_id))
                {
                    if (reorder_target_entity && dropped_entity->GetObjectId() != reorder_target_entity->GetObjectId())
                    {
                        // reorder: move entity to new position
                        spartan::Entity* target_parent = reorder_target_entity->GetParent();
                        spartan::Entity* dropped_parent = dropped_entity->GetParent();
                        
                        if (target_parent == dropped_parent)
                        {
                            // same parent - just reorder
                            if (target_parent)
                            {
                                std::vector<spartan::Entity*>& children = target_parent->GetChildren();
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
                                spartan::World::MoveRootEntityNear(dropped_entity, reorder_target_entity, reorder_insert_after);
                            }
                        }
                        else
                        {
                            // different parents - change parent and reorder
                            dropped_entity->SetParent(target_parent);
                            if (target_parent)
                            {
                                std::vector<spartan::Entity*>& children = target_parent->GetChildren();
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

void WorldViewer::TreeAddEntity(spartan::Entity* entity)
{
    // early exit if entity is null
    if (!entity)
        return;

    // set up tree node flags - we handle highlighting manually, so no SpanFullWidth or Selected
    ImGuiTreeNodeFlags node_flags            = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_OpenOnArrow;
    const vector<spartan::Entity*>& children = entity->GetChildren();
    bool has_children                        = !children.empty();
    if (!has_children)
    {
        node_flags |= ImGuiTreeNodeFlags_Leaf;
    }

    // handle selection (multi-select support)
    spartan::Camera* camera = spartan::World::GetCamera();
    const bool is_selected  = camera && camera->IsSelected(entity);
    spartan::Entity* primary_selected = camera ? camera->GetSelectedEntity() : nullptr;
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
    if (!spartan::Engine::IsFlagSet(spartan::EngineMode::Playing))
    {
        if (ImGui::BeginDragDropSource())
        {
            uint64_t entity_id = entity->GetObjectId();
            ImGui::SetDragDropPayload("ENTITY", &entity_id, sizeof(uint64_t));
            ImGui::Text("%s", entity->GetObjectName().c_str());
            ImGui::EndDragDropSource();
        }
    }

    // drop target - only handles parenting (middle zone)
    // reordering is handled by window-level drop target in the gaps
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            if (payload->DataSize != sizeof(uint64_t))
            {
                SP_LOG_ERROR("Invalid drag-and-drop payload size for entity.");
            }
            else
            {
                const uint64_t entity_id = *(const uint64_t*)payload->Data;
                if (spartan::Entity* dropped_entity = spartan::World::GetEntityById(entity_id))
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
                            spartan::Entity* old_parent = entity->GetParent();
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
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();

    // draw icon (still on foreground channel)
    ImVec2 icon_pos  = row_pos;
    ImTextureID icon = reinterpret_cast<ImTextureID>(component_to_image(entity));
    float next_x     = icon_pos.x;
    if (icon)
    {
        const float padding   = ImGui::GetStyle().FramePadding.y * 2.0f;
        const float icon_size = ImGui::GetTextLineHeightWithSpacing() - padding;
        const float y_offset  = (ImGui::GetTextLineHeightWithSpacing() - icon_size) * 0.25f;
        ImVec2 icon_min       = ImVec2(icon_pos.x, icon_pos.y + y_offset);
        ImVec2 icon_max       = ImVec2(icon_min.x + icon_size, icon_min.y + icon_size);
        dl->AddImage(icon, icon_min, icon_max);
        next_x                = icon_max.x + ImGui::GetStyle().ItemSpacing.x;
    }

    // draw text (still on foreground channel)
    const ImVec2 text_pos = ImVec2(next_x, row_pos.y - (ImGui::GetTextLineHeightWithSpacing() - ImGui::GetTextLineHeight()) * 0.25f);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_pos, ImGui::GetColorU32(ImGuiCol_Text), entity->GetObjectName().c_str());

    // merge channels before processing children (they will have their own channel splits)
    dl->ChannelsMerge();

    // note: selection is handled in OnTickVisible on mouse release to avoid double-selection

    // recursively add children
    if (is_node_open)
    {
        if (has_children)
        {
            for (const auto& child : children)
            {
                TreeAddEntity(spartan::World::GetEntityById(child->GetObjectId()));
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
        return;

    // double-click on item - Focus camera on entity
    if (double_click && entity_hovered)
    {
        selection_from_click = true;
        SetSelectedEntity(entity_hovered);
        entity_shift_anchor = entity_hovered;
        if (spartan::Camera* camera = spartan::World::GetCamera())
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
            if (spartan::Camera* camera = spartan::World::GetCamera())
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
        bool ctrl_held = spartan::Input::GetKey(spartan::KeyCode::Ctrl_Left) || spartan::Input::GetKey(spartan::KeyCode::Ctrl_Right);
        if (!ctrl_held)
        {
            SetSelectedEntity(nullptr);
            entity_shift_anchor = nullptr;
        }
    }
}

void WorldViewer::SetSelectedEntity(spartan::Entity* entity)
{
    // while in game mode the tree is not interactive, so don't allow selection
    bool is_in_game_mode = spartan::Engine::IsFlagSet(spartan::EngineMode::Playing);
    if (is_in_game_mode)
        return;

    if (spartan::Camera* camera = spartan::World::GetCamera())
    {
        camera->SetSelectedEntity(entity);
    }

    Properties::Inspect(entity);
}

void WorldViewer::Popups()
{
    PopupContextMenu();
    PopupEntityRename();
}

void WorldViewer::PopupContextMenu() const
{
    if (!ImGui::BeginPopup("##HierarchyContextMenu"))
        return;

    // get selected entities
    spartan::Camera* camera = spartan::World::GetCamera();
    spartan::Entity* selected_entity = camera ? camera->GetSelectedEntity() : nullptr;
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
        entity_copied->Clone();
    }

    if (ImGui::MenuItem("Rename") && on_entity && !multiple_selected)
    {
        popup_rename_entity = true;
    }

    if (ImGui::MenuItem("Focus") && on_entity)
    {
        spartan::World::GetCamera()->FocusOnSelectedEntity();
    }

    // delete shows count if multiple selected
    std::string delete_label = multiple_selected ? "Delete (" + std::to_string(selected_count) + ")" : "Delete";
    if (ImGui::MenuItem(delete_label.c_str(), "Delete") && on_entity)
    {
        if (multiple_selected)
        {
            // delete all selected entities
            std::vector<spartan::Entity*> to_delete = camera->GetSelectedEntities();
            for (spartan::Entity* entity : to_delete)
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

void WorldViewer::PopupEntityRename() const
{
    if (popup_rename_entity)
    {
        ImGui::OpenPopup("##RenameEntity");
        popup_rename_entity = false;
    }

    if (ImGui::BeginPopup("##RenameEntity"))
    {
        spartan::Entity* selected_entity = spartan::World::GetCamera()->GetSelectedEntity();
        if (!selected_entity)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        string name = selected_entity->GetObjectName();
        ImGui::Text("Name:");
        ImGui::InputText("##edit", &name);
        selected_entity->SetObjectName(string(name));

        if (ImGuiSp::button("Ok"))
        { 
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        ImGui::EndPopup();
    }
}

void WorldViewer::HandleKeyShortcuts()
{
    // Delete - deletes all selected entities
    if (spartan::Input::GetKey(spartan::KeyCode::Delete))
    {
        if (spartan::Camera* camera = spartan::World::GetCamera())
        {
            // copy the vector since we're modifying it
            std::vector<spartan::Entity*> to_delete = camera->GetSelectedEntities();
            for (spartan::Entity* entity : to_delete)
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
    if (spartan::Input::GetKey(spartan::KeyCode::Ctrl_Left) && spartan::Input::GetKeyDown(spartan::KeyCode::S))
    {
        const string& file_path = spartan::World::GetFilePath();

        if (file_path.empty())
        {
            m_editor->GetWidget<MenuBar>()->ShowWorldSaveDialog();
        }
        else
        {
            spartan::ThreadPool::AddTask([]()
            {
                spartan::World::SaveToFile(spartan::World::GetFilePath());
            });
        }
    }

    // Load: Ctrl + L
    if (spartan::Input::GetKey(spartan::KeyCode::Ctrl_Left) && spartan::Input::GetKeyDown(spartan::KeyCode::L))
    {
        m_editor->GetWidget<MenuBar>()->ShowWorldLoadDialog();
    }

    // Undo and Redo: Ctrl + Z, Ctrl + Shift + Z
    if (spartan::Input::GetKey(spartan::KeyCode::Ctrl_Left) && spartan::Input::GetKeyDown(spartan::KeyCode::Z))
    {
        if (spartan::Input::GetKey(spartan::KeyCode::Shift_Left))
        {
            spartan::CommandStack::Redo();
        }
        else
        {
            spartan::CommandStack::Undo();
        }
    }

    // Copy: Ctrl + C
    if (spartan::Input::GetKey(spartan::KeyCode::Ctrl_Left) && spartan::Input::GetKeyDown(spartan::KeyCode::C))
    {
        if (spartan::Camera* camera = spartan::World::GetCamera())
        {
            if (spartan::Entity* selected_entity = camera->GetSelectedEntity())
            {
                entity_copied = selected_entity;
            }
        }
    }

    // Paste: Ctrl + V
    if (spartan::Input::GetKey(spartan::KeyCode::Ctrl_Left) && spartan::Input::GetKeyDown(spartan::KeyCode::V))
    {
        if (entity_copied)
        {
            entity_copied->Clone();
        }
    }
}

void WorldViewer::ActionEntityDelete(spartan::Entity* entity)
{
    SP_ASSERT_MSG(entity != nullptr, "Entity is null");

    // check if entity still exists (might have been deleted as a child of another entity)
    if (!spartan::World::EntityExists(entity))
        return;

    // create undo command (stores entity state before deletion)
    auto command = std::make_shared<spartan::CommandEntityDelete>(entity);

    // delete the entity
    command->OnApply();

    // push to undo stack
    spartan::CommandStack::Push(command);
}

spartan::Entity* WorldViewer::ActionEntityCreateEmpty()
{
    spartan::Entity* entity = spartan::World::CreateEntity();
    
    if (spartan::Camera* camera = spartan::World::GetCamera())
    {
        if (spartan::Entity* selected_entity = camera->GetSelectedEntity())
        {
            entity->SetParent(selected_entity);
        }
    }

    return entity;
}

void WorldViewer::ActionEntityCreateCube()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<spartan::Renderable>();
    renderable->SetMesh(spartan::MeshType::Cube);
    renderable->SetDefaultMaterial();
    entity->SetObjectName("Cube");
}

void WorldViewer::ActionEntityCreateQuad()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<spartan::Renderable>();
    renderable->SetMesh(spartan::MeshType::Quad);
    renderable->SetDefaultMaterial();
    entity->SetObjectName("Quad");
}

void WorldViewer::ActionEntityCreateSphere()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<spartan::Renderable>();
    renderable->SetMesh(spartan::MeshType::Sphere);
    renderable->SetDefaultMaterial();
    entity->SetObjectName("Sphere");
}

void WorldViewer::ActionEntityCreateCylinder()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<spartan::Renderable>();
    renderable->SetMesh(spartan::MeshType::Cylinder);
    renderable->SetDefaultMaterial();
    entity->SetObjectName("Cylinder");
}

void WorldViewer::ActionEntityCreateCone()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<spartan::Renderable>();
    renderable->SetMesh(spartan::MeshType::Cone);
    renderable->SetDefaultMaterial();
    entity->SetObjectName("Cone");
}

void WorldViewer::ActionEntityCreateCamera()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<spartan::Camera>();
    entity->SetObjectName("Camera");
}

void WorldViewer::ActionEntityCreateTerrain()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<spartan::Terrain>();
    entity->SetObjectName("Terrain");
}

void WorldViewer::ActionEntityCreateLightDirectional()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<spartan::Light>()->SetLightType(spartan::LightType::Directional);
    entity->SetObjectName("Directional");
}

void WorldViewer::ActionEntityCreateLightPoint()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetObjectName("Point");

    spartan::Light* light = entity->AddComponent<spartan::Light>();
    light->SetLightType(spartan::LightType::Point);
    light->SetIntensity(spartan::LightIntensity::bulb_150_watt);
}

void WorldViewer::ActionEntityCreateLightSpot()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetObjectName("Spot");

    spartan::Light* light = entity->AddComponent<spartan::Light>();
    light->SetLightType(spartan::LightType::Spot);
    light->SetIntensity(spartan::LightIntensity::bulb_150_watt);
}

void WorldViewer::ActionEntityCreateLightArea()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetObjectName("Area");

    spartan::Light* light = entity->AddComponent<spartan::Light>();
    light->SetLightType(spartan::LightType::Area);
    light->SetIntensity(spartan::LightIntensity::bulb_150_watt);
}

void WorldViewer::ActionEntityCreatePhysicsBody()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<spartan::Physics>();
    entity->SetObjectName("Physics");
}

void WorldViewer::ActionEntityCreateAudioSource()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<spartan::AudioSource>();
    entity->SetObjectName("Physics");
}
