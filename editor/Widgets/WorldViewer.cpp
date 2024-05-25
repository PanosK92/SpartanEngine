#/*
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

//= INCLUDES ==============================
#include "WorldViewer.h"
#include "Properties.h"
#include "TitleBar.h"
#include "Viewport.h"
#include "World/Entity.h"
#include "World/Components/Light.h"
#include "World/Components/AudioSource.h"
#include "World/Components/AudioListener.h"
#include "World/Components/PhysicsBody.h"
#include "World/Components/Constraint.h"
#include "World/Components/Terrain.h"
#include "Commands/CommandStack.h"
#include "../ImGui/ImGuiExtension.h"
SP_WARNINGS_OFF
#include "../ImGui/Source/imgui_stdlib.h"
SP_WARNINGS_ON
//=========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    bool popup_rename_entity       = false;
    Spartan::Entity* entity_copied = nullptr;
    weak_ptr <Spartan::Entity> entity_clicked;
    weak_ptr <Spartan::Entity> entity_hovered;
    ImGuiSp::DragDropPayload g_payload;

    void world_selection_window(Editor* editor)
    {
        static bool is_default_world_window_visible = true;
        if (is_default_world_window_visible)
        {
            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin("World selection", &is_default_world_window_visible, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar))
            {
                ImGui::Text("Select the world you would like to load and click \"Ok\"");
    
                // list
                static int item_index = 0;
                {
                    static const char* items[] =
                    {
                        "1. Objects",
                        "2. Car",
                        "3. Forest",
                        "4. Sponza",
                        "5. Doom",
                        "6. Bistro",
                        "7. Minecraft",
                        "8. Living Room"
                    };
                    static int item_count = IM_ARRAYSIZE(items);
             
                    ImGui::PushItemWidth(500.0f * Spartan::Window::GetDpiScale());
                    ImGui::ListBox("##list_box", &item_index, items, item_count, item_count);
                    ImGui::PopItemWidth();
                }

                // button
                if (ImGuiSp::button_centered_on_line("Ok"))
                {
                    Spartan::World::LoadDefaultWorld(static_cast<Spartan::DefaultWorld>(item_index));
                    is_default_world_window_visible = false;
                }
            }
            ImGui::End();
        }
    }
}

WorldViewer::WorldViewer(Editor* editor) : Widget(editor)
{
    m_title = "World";
    m_flags |= ImGuiWindowFlags_HorizontalScrollbar;
}

void WorldViewer::OnTickVisible()
{
    TreeShow();

    // On left click, select entity but only on release
    if (ImGui::IsMouseReleased(0))
    {
        // Make sure that the mouse was released while on the same entity
        if (shared_ptr<Spartan::Entity> entity_clicked_raw = entity_clicked.lock())
        {
            if (shared_ptr<Spartan::Entity> entity_hovered_raw = entity_hovered.lock())
            {
                if (entity_hovered_raw->GetObjectId() == entity_clicked_raw->GetObjectId())
                {
                    SetSelectedEntity(entity_clicked_raw);
                }

                entity_clicked.reset();
            }
        }
    }

    world_selection_window(m_editor);
}

void WorldViewer::TreeShow()
{
    OnTreeBegin();

    if (ImGui::TreeNodeEx("Root", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
    {
        // dropping on the scene node should unparent the entity
        if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Entity))
        {
            const uint64_t entity_id = get<uint64_t>(payload->data);
            if (const shared_ptr<Spartan::Entity>& dropped_entity = Spartan::World::GetEntityById(entity_id))
            {
                shared_ptr<Spartan::Entity> null = nullptr;
                dropped_entity->SetParent(null);
            }
        }

        vector<shared_ptr<Spartan::Entity>> root_entities = Spartan::World::GetRootEntities();
        for (const shared_ptr<Spartan::Entity>& entity : root_entities)
        {
            if (entity->IsActive())
            {
                TreeAddEntity(entity);
            }
        }

        // if we have been expanding to show an entity and no more expansions are taking place, we reached it
        // so, we stop expanding and we bring it into view
        if (m_expand_to_selection && !m_expanded_to_selection)
        {
            ImGui::ScrollToBringRectIntoView(m_window, m_selected_entity_rect);
            m_expand_to_selection = false;
        }

        ImGui::TreePop();
    }

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

void WorldViewer::TreeAddEntity(shared_ptr<Spartan::Entity> entity)
{
    if (!entity)
        return;

    m_expanded_to_selection            = false;
    bool is_selected_entity            = false;
    const bool is_visible_in_hierarchy = entity->IsVisibleInHierarchy();
    bool has_visible_children          = false;

    // Don't draw invisible entities
    if (!is_visible_in_hierarchy)
        return;

    // Determine children visibility
    const vector<Spartan::Entity*>& children = entity->GetChildren();
    for (Spartan::Entity* child : children)
    {
        if (child->IsVisibleInHierarchy())
        {
            has_visible_children = true;
            break;
        }
    }

    // Flags
    ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanFullWidth;

    // Flag - Is expandable (has children) ?
    node_flags |= has_visible_children ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf; 

    // Flag - Is selected?
    if (shared_ptr<Spartan::Camera> camera = Spartan::Renderer::GetCamera())
    {
        if (shared_ptr<Spartan::Entity> selected_entity = camera->GetSelectedEntity())
        {
            node_flags |= selected_entity->GetObjectId() == entity->GetObjectId() ? ImGuiTreeNodeFlags_Selected : node_flags;

            if (m_expand_to_selection)
            {
                // If the selected entity is a descendant of the this entity, start expanding (this can happen if an entity is selected in the viewport)
                if (selected_entity->IsDescendantOf(entity.get()))
                {
                    ImGui::SetNextItemOpen(true);
                    m_expanded_to_selection = true;
                }
            }
        }
    }

    // Add node
    const void* node_id     = reinterpret_cast<void*>(static_cast<uint64_t>(entity->GetObjectId()));
    string node_name        = entity->GetObjectName();
    const bool is_node_open = ImGui::TreeNodeEx(node_id, node_flags, node_name.c_str());

    // Keep a copy of the selected item's rect so that we can scroll to bring it into view
    if ((node_flags & ImGuiTreeNodeFlags_Selected) && m_expand_to_selection)
    {
        m_selected_entity_rect = ImGui::GetCurrentContext()->LastItemData.Rect;
    }

    // Manually detect some useful states
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
    {
        entity_hovered = entity;
    }

    EntityHandleDragDrop(entity);

    // Recursively show all child nodes
    if (is_node_open)
    {
        if (has_visible_children)
        {
            for (const auto& child : children)
            {
                if (!child->IsVisibleInHierarchy())
                    continue;

                TreeAddEntity(Spartan::World::GetEntityById(child->GetObjectId()));
            }
        }

        // Pop if isNodeOpen
        ImGui::TreePop();
    }
}

void WorldViewer::HandleClicking()
{
    const auto is_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const auto left_click        = ImGui::IsMouseClicked(0);
    const auto right_click       = ImGui::IsMouseClicked(1);

    // Since we are handling clicking manually, we must ensure we are inside the window
    if (!is_window_hovered)
        return;    

    // Left click on item - Don't select yet
    if (left_click && entity_hovered.lock())
    {
        entity_clicked = entity_hovered;
    }

    // Right click on item - Select and show context menu
    if (ImGui::IsMouseClicked(1))
    {
        if (shared_ptr<Spartan::Entity> entity = entity_hovered.lock())
        {            
            SetSelectedEntity(entity);
        }

        ImGui::OpenPopup("##HierarchyContextMenu");
    }

    // Clicking on empty space - Clear selection
    if ((left_click || right_click) && !entity_hovered.lock())
    {
        SetSelectedEntity(m_entity_empty);
    }
}

void WorldViewer::EntityHandleDragDrop(shared_ptr<Spartan::Entity> entity_ptr) const
{
    // Drag
    if (ImGui::BeginDragDropSource())
    {
        g_payload.data = entity_ptr->GetObjectId();
        g_payload.type = ImGuiSp::DragPayloadType::Entity;
        ImGuiSp::create_drag_drop_paylod(g_payload);
        ImGui::EndDragDropSource();
    }
    // Drop
    if (auto payload = ImGuiSp::receive_drag_drop_payload(ImGuiSp::DragPayloadType::Entity))
    {
        const uint64_t entity_id = get<uint64_t>(payload->data);
        if (const shared_ptr<Spartan::Entity>& dropped_entity = Spartan::World::GetEntityById(entity_id))
        {
            if (dropped_entity->GetObjectId() != entity_ptr->GetObjectId())
            {
                dropped_entity->SetParent(entity_ptr);
            }
        }
    }
}

void WorldViewer::SetSelectedEntity(const std::shared_ptr<Spartan::Entity> entity)
{
    m_expand_to_selection = true;

    if (shared_ptr<Spartan::Camera> camera = Spartan::Renderer::GetCamera())
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

    // Get selected entity
    shared_ptr<Spartan::Entity> selected_entity = nullptr;
    if (shared_ptr<Spartan::Camera> camera = Spartan::Renderer::GetCamera())
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
        Spartan::Renderer::GetCamera()->FocusOnSelectedEntity();
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
        else if (ImGui::MenuItem("Constraint"))
        {
            ActionEntityCreateConstraint();
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
        else if (ImGui::MenuItem("Audio Listener"))
        {
            ActionEntityCreateAudioListener();
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
        shared_ptr<Spartan::Entity> selected_entity = Spartan::Renderer::GetCamera()->GetSelectedEntity();
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
    if (Spartan::Input::GetKey(Spartan::KeyCode::Delete))
    {
        if (shared_ptr<Spartan::Camera> camera = Spartan::Renderer::GetCamera())
        { 
            if (shared_ptr<Spartan::Entity> selected_entity = camera->GetSelectedEntity())
            {
                ActionEntityDelete(selected_entity);
            }
        }
    }

    // Save: Ctrl + S
    if (Spartan::Input::GetKey(Spartan::KeyCode::Ctrl_Left) && Spartan::Input::GetKeyDown(Spartan::KeyCode::S))
    {
        const string& file_path = Spartan::World::GetFilePath();

        if (file_path.empty())
        {
            m_editor->GetWidget<TitleBar>()->ShowWorldSaveDialog();
        }
        else
        {
            EditorHelper::SaveWorld(Spartan::World::GetFilePath());
        }
    }

    // Load: Ctrl + L
    if (Spartan::Input::GetKey(Spartan::KeyCode::Ctrl_Left) && Spartan::Input::GetKeyDown(Spartan::KeyCode::L))
    {
        m_editor->GetWidget<TitleBar>()->ShowWorldLoadDialog();
    }

    // Undo and Redo: Ctrl + Z, Ctrl + Shift + Z
    if (Spartan::Input::GetKey(Spartan::KeyCode::Ctrl_Left) && Spartan::Input::GetKeyDown(Spartan::KeyCode::Z))
    {
        if (Spartan::Input::GetKey(Spartan::KeyCode::Shift_Left))
        {
            Spartan::CommandStack::Redo();
        }
        else
        {
            Spartan::CommandStack::Undo();
        }
    }
}

void WorldViewer::ActionEntityDelete(const shared_ptr<Spartan::Entity> entity)
{
    SP_ASSERT_MSG(entity != nullptr, "Entity is null");

    Spartan::World::RemoveEntity(entity.get());
}

Spartan::Entity* WorldViewer::ActionEntityCreateEmpty()
{
    shared_ptr<Spartan::Entity> entity = Spartan::World::CreateEntity();
    
    if (shared_ptr<Spartan::Camera> camera = Spartan::Renderer::GetCamera())
    {
        if (shared_ptr<Spartan::Entity> selected_entity = camera->GetSelectedEntity())
        {
            entity->SetParent(selected_entity);
        }
    }

    return entity.get();
}

void WorldViewer::ActionEntityCreateCube()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Spartan::Renderable>();
    renderable->SetGeometry(Spartan::MeshType::Cube);
    renderable->SetDefaultMaterial();
    entity->SetObjectName("Cube");
}

void WorldViewer::ActionEntityCreateQuad()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Spartan::Renderable>();
    renderable->SetGeometry(Spartan::MeshType::Quad);
    renderable->SetDefaultMaterial();
    entity->SetObjectName("Quad");
}

void WorldViewer::ActionEntityCreateSphere()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Spartan::Renderable>();
    renderable->SetGeometry(Spartan::MeshType::Sphere);
    renderable->SetDefaultMaterial();
    entity->SetObjectName("Sphere");
}

void WorldViewer::ActionEntityCreateCylinder()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Spartan::Renderable>();
    renderable->SetGeometry(Spartan::MeshType::Cylinder);
    renderable->SetDefaultMaterial();
    entity->SetObjectName("Cylinder");
}

void WorldViewer::ActionEntityCreateCone()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Spartan::Renderable>();
    renderable->SetGeometry(Spartan::MeshType::Cone);
    renderable->SetDefaultMaterial();
    entity->SetObjectName("Cone");
}

void WorldViewer::ActionEntityCreateCamera()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Spartan::Camera>();
    entity->SetObjectName("Camera");
}

void WorldViewer::ActionEntityCreateTerrain()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Spartan::Terrain>();
    entity->SetObjectName("Terrain");
}

void WorldViewer::ActionEntityCreateLightDirectional()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Spartan::Light>()->SetLightType(Spartan::LightType::Directional);
    entity->SetObjectName("Directional");
}

void WorldViewer::ActionEntityCreateLightPoint()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetObjectName("Point");

    shared_ptr<Spartan::Light> light = entity->AddComponent<Spartan::Light>();
    light->SetLightType(Spartan::LightType::Point);
    light->SetIntensity(Spartan::LightIntensity::bulb_150_watt);
}

void WorldViewer::ActionEntityCreateLightSpot()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetObjectName("Spot");

    shared_ptr<Spartan::Light> light = entity->AddComponent<Spartan::Light>();
    light->SetLightType(Spartan::LightType::Spot);
    light->SetIntensity(Spartan::LightIntensity::bulb_150_watt);
}

void WorldViewer::ActionEntityCreatePhysicsBody()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Spartan::PhysicsBody>();
    entity->SetObjectName("PhysicsBody");
}

void WorldViewer::ActionEntityCreateConstraint()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Spartan::Constraint>();
    entity->SetObjectName("Constraint");
}

void WorldViewer::ActionEntityCreateAudioSource()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Spartan::AudioSource>();
    entity->SetObjectName("AudioSource");
}

void WorldViewer::ActionEntityCreateAudioListener()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Spartan::AudioListener>();
    entity->SetObjectName("AudioListener");
}
