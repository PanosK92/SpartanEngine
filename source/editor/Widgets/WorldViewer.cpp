/*
Copyright(c) 2015-2025 Panos Karabelas

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
    weak_ptr <spartan::Entity> entity_clicked;
    weak_ptr <spartan::Entity> entity_hovered;
    ImGuiSp::DragDropPayload drag_drop_payload;
    bool popup_rename_entity       = false;
    spartan::Entity* entity_copied = nullptr;
    ImRect selected_entity_rect;
    uint64_t last_selected_entity_id = 0;

    spartan::RHI_Texture* component_to_image(const shared_ptr<spartan::Entity>& entity)
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
        if (shared_ptr<spartan::Entity> entity_clicked_raw = entity_clicked.lock())
        {
            if (shared_ptr<spartan::Entity> entity_hovered_raw = entity_hovered.lock())
            {
                if (entity_hovered_raw->GetObjectId() == entity_clicked_raw->GetObjectId())
                {
                    SetSelectedEntity(entity_clicked_raw);
                }

                entity_clicked.reset();
            }
        }
    }
}

void WorldViewer::TreeShow()
{
    OnTreeBegin();

    bool is_in_game_mode = spartan::Engine::IsFlagSet(spartan::EngineMode::Playing);
    ImGui::BeginDisabled(is_in_game_mode);
    {
        static vector<shared_ptr<spartan::Entity>> root_entities;
        spartan::World::GetRootEntities(root_entities);

        // iterate over root entities directly, omitting the root node
        for (const shared_ptr<spartan::Entity>& entity : root_entities)
        {
            if (entity->GetActive())
            {
                TreeAddEntity(entity);
            }
        }
    }
    ImGui::EndDisabled();

    OnTreeEnd();
}

void WorldViewer::OnTreeBegin()
{
    entity_hovered.reset();
}

void WorldViewer::OnTreeEnd()
{
    HandleKeyShortcuts();
    HandleClicking();
    Popups();
}

void WorldViewer::TreeAddEntity(shared_ptr<spartan::Entity> entity)
{
    // early exit if entity is null
    if (!entity)
        return;

    // set up tree node flags
    ImGuiTreeNodeFlags node_flags            = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    const vector<spartan::Entity*>& children = entity->GetChildren();
    bool has_children                        = !children.empty();
    if (!has_children)
    {
        node_flags |= ImGuiTreeNodeFlags_Leaf;
    }

    // handle selection
    shared_ptr<spartan::Entity> selected_entity = spartan::World::GetCamera() ? spartan::World::GetCamera()->GetSelectedEntity() : nullptr;
    const bool is_selected                      = selected_entity && selected_entity->GetObjectId() == entity->GetObjectId();
    const bool first_time_selected              = is_selected && selected_entity->GetObjectId() != last_selected_entity_id;
    if (is_selected)
    {
        node_flags |= ImGuiTreeNodeFlags_Selected;
    }

    // auto-expand for selected descendants
    if (selected_entity && selected_entity->IsDescendantOf(entity.get()) && selected_entity->GetObjectId() != last_selected_entity_id)
    {
        ImGui::SetNextItemOpen(true);
    }

    // start tree node
    const void* node_id     = reinterpret_cast<void*>(static_cast<uint64_t>(entity->GetObjectId()));
    const bool is_node_open = ImGui::TreeNodeEx(node_id, node_flags, "");

    // scroll to selected entity
    if (first_time_selected)
    {
        ImGui::SetScrollHereY(0.25f);
        last_selected_entity_id = selected_entity->GetObjectId();
    }

    // set up row for interaction
    ImGui::SameLine();
    const ImVec2 row_pos  = ImGui::GetCursorScreenPos();
    const ImVec2 row_size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeightWithSpacing());

    // handle clicking and drag-and-drop
    ImGui::PushID(node_id);
    ImGui::InvisibleButton("row_btn", row_size);
    const bool clicked         = ImGui::IsItemClicked();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
    {
        entity_hovered = entity;
    }

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

    // drop target
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
                if (const shared_ptr<spartan::Entity> dropped_entity = spartan::World::GetEntityById(entity_id))
                {
                    if (dropped_entity->GetObjectId() != entity->GetObjectId())
                    {
                        // reverse parent-child if dropped entity is a direct child
                        if (entity->GetParent() == dropped_entity.get())
                        {
                            entity->SetParent(nullptr); // temporarily unparent to avoid cycle
                            dropped_entity->SetParent(entity.get());
                        }
                        // reverse parent-child if dropped entity is a descendant
                        else if (entity->IsDescendantOf(dropped_entity.get()))
                        {
                            spartan::Entity* old_parent = entity->GetParent();
                            if (!old_parent || old_parent != dropped_entity.get())
                            {
                                entity->SetParent(dropped_entity.get());
                            }
                            else
                            {
                                SP_LOG_WARNING("cannot make %s a child of %s due to circular parenting.", entity->GetObjectName().c_str(), dropped_entity->GetObjectName().c_str());
                            }
                        }
                        // normal case: make dropped entity a child of target
                        else
                        {
                            dropped_entity->SetParent(entity.get());
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

    // draw icon
    ImVec2 icon_pos  = row_pos;
    ImTextureID icon = reinterpret_cast<ImTextureID>(component_to_image(entity));
    float next_x     = icon_pos.x;
    ImDrawList* dl   = ImGui::GetWindowDrawList();
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

    // draw text
    const ImVec2 text_pos = ImVec2(next_x, row_pos.y - (ImGui::GetTextLineHeightWithSpacing() - ImGui::GetTextLineHeight()) * 0.25f);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_pos, ImGui::GetColorU32(ImGuiCol_Text), entity->GetObjectName().c_str());

    // handle selection on click
    if (clicked)
    {
        if (spartan::Camera* camera = spartan::World::GetCamera())
        {
            camera->SetSelectedEntity(entity);
        }
    }

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

    // since we are handling clicking manually, we must ensure we are inside the window
    if (!is_window_hovered)
        return;

    // left click on item - Don't select yet
    if (left_click && entity_hovered.lock())
    {
        entity_clicked = entity_hovered;
    }

    // right click on item - Select and show context menu
    if (ImGui::IsMouseClicked(1))
    {
        if (shared_ptr<spartan::Entity> entity = entity_hovered.lock())
        {
            SetSelectedEntity(entity);
        }

        ImGui::OpenPopup("##HierarchyContextMenu");
    }

    // clicking on empty space - Clear selection
    if ((left_click || right_click) && !entity_hovered.lock())
    {
        SetSelectedEntity(m_entity_empty);
    }
}

void WorldViewer::SetSelectedEntity(const std::shared_ptr<spartan::Entity> entity)
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

    // get selected entity
    shared_ptr<spartan::Entity> selected_entity = nullptr;
    if (spartan::Camera* camera = spartan::World::GetCamera())
    {
        selected_entity = camera->GetSelectedEntity();
    }

    const bool on_entity = selected_entity != nullptr;

    if (ImGui::MenuItem("Copy") && on_entity)
    {
        entity_copied = selected_entity.get();
    }

    if (ImGui::MenuItem("Paste") && entity_copied)
    {
        entity_copied->Clone();
    }

    if (ImGui::MenuItem("Rename") && on_entity)
    {
        popup_rename_entity = true;
    }

    if (ImGui::MenuItem("Focus") && on_entity)
    {
        spartan::World::GetCamera()->FocusOnSelectedEntity();
    }

    if (ImGui::MenuItem("Delete", "Delete") && on_entity)
    {
        ActionEntityDelete(selected_entity);
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
        shared_ptr<spartan::Entity> selected_entity = spartan::World::GetCamera()->GetSelectedEntity();
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
    // Delete
    if (spartan::Input::GetKey(spartan::KeyCode::Delete))
    {
        if (spartan::Camera* camera = spartan::World::GetCamera())
        { 
            if (shared_ptr<spartan::Entity> selected_entity = camera->GetSelectedEntity())
            {
                ActionEntityDelete(selected_entity);
            }
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
}

void WorldViewer::ActionEntityDelete(const shared_ptr<spartan::Entity> entity)
{
    SP_ASSERT_MSG(entity != nullptr, "Entity is null");

    spartan::World::RemoveEntity(entity.get());
}

spartan::Entity* WorldViewer::ActionEntityCreateEmpty()
{
    shared_ptr<spartan::Entity> entity = spartan::World::CreateEntity();
    
    if (spartan::Camera* camera = spartan::World::GetCamera())
    {
        if (shared_ptr<spartan::Entity> selected_entity = camera->GetSelectedEntity())
        {
            entity->SetParent(selected_entity.get());
        }
    }

    return entity.get();
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
