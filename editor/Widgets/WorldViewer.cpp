#/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ================================
#include "WorldViewer.h"
#include "Properties.h"
#include "MenuBar.h"
#include "../Editor.h"
#include "../ImGui/ImGuiExtension.h"
#include "../ImGui/Source/imgui_stdlib.h"
#include "World/Entity.h"
#include "World/Components/Transform.h"
#include "World/Components/Light.h"
#include "World/Components/AudioSource.h"
#include "World/Components/AudioListener.h"
#include "World/Components/RigidBody.h"
#include "World/Components/SoftBody.h"
#include "World/Components/Collider.h"
#include "World/Components/Constraint.h"
#include "World/Components/Renderable.h"
#include "World/Components/Environment.h"
#include "World/Components/Terrain.h"
#include "World/Components/ReflectionProbe.h"
//===========================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================

namespace _Widget_World
{
    static Spartan::World* g_world  = nullptr;
    static Input* g_input           = nullptr;
    static bool g_popupRenameentity = false;
    static imgui_extension::DragDropPayload g_payload;
    // entities in relation to mouse events
    static Entity* g_entity_copied  = nullptr;
    static Entity* g_entity_hovered = nullptr;
    static Entity* g_entity_clicked = nullptr;
}

WorldViewer::WorldViewer(Editor* editor) : Widget(editor)
{
    m_title = "World";
    m_flags |= ImGuiWindowFlags_HorizontalScrollbar;

    _Widget_World::g_world = m_context->GetSystem<Spartan::World>();
    _Widget_World::g_input = m_context->GetSystem<Input>();

    // Subscribe to entity clicked engine event
    EditorHelper::Get().g_on_entity_selected = [this](){ SetSelectedEntity(EditorHelper::Get().g_selected_entity.lock(), false); };
}

static void load_default_world_startup_window(World* world)
{
    static bool is_visible = true;
    if (is_visible)
    {
        // Set position
        ImVec2 position     = ImVec2(Spartan::Display::GetWidth() * 0.5f, Spartan::Display::GetHeight() * 0.5f);
        ImVec2 pivot_center = ImVec2(0.5f, 0.5f);
        ImGui::SetNextWindowPos(position, ImGuiCond_Appearing, pivot_center);

        // Begin
        if (ImGui::Begin("Default World", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::Text("Would you like to load a default world?");

            if (imgui_extension::button_centered_on_line("Yes", 0.4f))
            {
                is_visible = false;
                ThreadPool::AddTask([world]()
                {
                    world->CreateDefaultWorld();
                });
            }

            ImGui::SameLine();

            if (ImGui::Button("No"))
            {
                is_visible = false;
            }
        }

        ImGui::End();
    }
}

void WorldViewer::TickVisible()
{
    TreeShow();

    // On left click, select entity but only on release
    if (ImGui::IsMouseReleased(0) && _Widget_World::g_entity_clicked)
    {
        // Make sure that the mouse was released while on the same entity
        if (_Widget_World::g_entity_hovered && _Widget_World::g_entity_hovered->GetObjectId() == _Widget_World::g_entity_clicked->GetObjectId())
        {
            SetSelectedEntity(_Widget_World::g_entity_clicked->GetPtrShared());
        }
        _Widget_World::g_entity_clicked = nullptr;
    }

    load_default_world_startup_window(_Widget_World::g_world);
}

void WorldViewer::TreeShow()
{
    OnTreeBegin();

    if (ImGui::TreeNodeEx("Root", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
    {
        // Dropping on the scene node should unparent the entity
        if (auto payload = imgui_extension::receive_drag_drop_payload(imgui_extension::DragPayloadType::DragPayload_Entity))
        {
            const uint64_t entity_id = get<uint64_t>(payload->data);
            if (const shared_ptr<Entity>& dropped_entity = _Widget_World::g_world->GetEntityById(entity_id))
            {
                dropped_entity->GetTransform()->SetParent(nullptr);
            }
        }

        vector<shared_ptr<Entity>> root_entities = _Widget_World::g_world->GetRootEntities();
        for (const shared_ptr<Entity>& entity : root_entities)
        {
            TreeAddEntity(entity.get());
        }

        // If we have been expanding to show an entity and no more expansions are taking place, we reached it.
        // So, we stop expanding and we bring it into view.
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
    _Widget_World::g_entity_hovered = nullptr;
}

void WorldViewer::OnTreeEnd()
{
    HandleKeyShortcuts();
    HandleClicking();
    Popups();
}

void WorldViewer::TreeAddEntity(Entity* entity)
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
    const vector<Transform*>& children = entity->GetTransform()->GetChildren();
    for (Transform* child : children)
    {
        if (child->GetEntity()->IsVisibleInHierarchy())
        {
            has_visible_children = true;
            break;
        }
    }

    // Flags
    ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanFullWidth;

    // Flag - Is expandable (has children) ?
    node_flags |= has_visible_children ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf; 

    // Flag - Is selected?
    if (const shared_ptr<Entity> selected_entity = EditorHelper::Get().g_selected_entity.lock())
    {
        node_flags |= selected_entity->GetObjectId() == entity->GetObjectId() ? ImGuiTreeNodeFlags_Selected : node_flags;

        if (m_expand_to_selection)
        {
            // If the selected entity is a descendant of the this entity, start expanding (this can happen if an entity is selected in the viewport)
            if (selected_entity->GetTransform()->IsDescendantOf(entity->GetTransform()))
            {
                ImGui::SetNextItemOpen(true);
                m_expanded_to_selection = true;
            }
        }
    }

    // Add node
    const void* node_id     = reinterpret_cast<void*>(static_cast<uint64_t>(entity->GetObjectId()));
    string node_name        = entity->GetName();
    const bool is_node_open = ImGui::TreeNodeEx(node_id, node_flags, node_name.c_str());

    // Keep a copy of the selected item's rect so that we can scroll to bring it into view
    if ((node_flags & ImGuiTreeNodeFlags_Selected) && m_expand_to_selection)
    {
        m_selected_entity_rect = ImGui::GetCurrentContext()->LastItemData.Rect;
    }

    // Manually detect some useful states
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
    {
        _Widget_World::g_entity_hovered = entity;
    }

    EntityHandleDragDrop(entity);

    // Recursively show all child nodes
    if (is_node_open)
    {
        if (has_visible_children)
        {
            for (const auto& child : children)
            {
                if (!child->GetEntity()->IsVisibleInHierarchy())
                    continue;

                TreeAddEntity(child->GetEntity());
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
    if (left_click && _Widget_World::g_entity_hovered)
    {
        _Widget_World::g_entity_clicked    = _Widget_World::g_entity_hovered;
    }

    // Right click on item - Select and show context menu
    if (ImGui::IsMouseClicked(1))
    {
        if (_Widget_World::g_entity_hovered)
        {            
            SetSelectedEntity(_Widget_World::g_entity_hovered->GetPtrShared());
        }

        ImGui::OpenPopup("##HierarchyContextMenu");
    }

    // Clicking on empty space - Clear selection
    if ((left_click || right_click) && !_Widget_World::g_entity_hovered)
    {
        SetSelectedEntity(m_entity_empty);
    }
}

void WorldViewer::EntityHandleDragDrop(Entity* entity_ptr) const
{
    // Drag
    if (ImGui::BeginDragDropSource())
    {
        _Widget_World::g_payload.data = entity_ptr->GetObjectId();
        _Widget_World::g_payload.type = imgui_extension::DragPayloadType::DragPayload_Entity;
        imgui_extension::create_drag_drop_paylod(_Widget_World::g_payload);
        ImGui::EndDragDropSource();
    }
    // Drop
    if (auto payload = imgui_extension::receive_drag_drop_payload(imgui_extension::DragPayloadType::DragPayload_Entity))
    {
        const uint64_t entity_id = get<uint64_t>(payload->data);
        if (const shared_ptr<Entity>& dropped_entity = _Widget_World::g_world->GetEntityById(entity_id))
        {
            if (dropped_entity->GetObjectId() != entity_ptr->GetObjectId())
            {
                dropped_entity->GetTransform()->SetParent(entity_ptr->GetTransform());
            }
        }
    }
}

void WorldViewer::SetSelectedEntity(const shared_ptr<Entity>& entity, const bool from_editor /*= true*/)
{
    m_expand_to_selection = true;

    // If the update comes from this widget, let the engine know about it
    if (from_editor)
    {
        EditorHelper::Get().SetSelectedEntity(entity);
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

    const auto selected_entity    = EditorHelper::Get().g_selected_entity.lock();
    const auto on_entity        = selected_entity != nullptr;

    if (on_entity) if (ImGui::MenuItem("Copy"))
    {
        _Widget_World::g_entity_copied = selected_entity.get();
    }

    if (ImGui::MenuItem("Paste"))
    {
        if (_Widget_World::g_entity_copied)
        {
            _Widget_World::g_entity_copied->Clone();
        }
    }

    if (on_entity) if (ImGui::MenuItem("Rename"))
    {
        _Widget_World::g_popupRenameentity = true;
    }

    if (on_entity) if (ImGui::MenuItem("Delete", "Delete"))
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
        if (ImGui::MenuItem("Rigid Body"))
        {
            ActionEntityCreateRigidBody();
        }
        else if (ImGui::MenuItem("Soft Body"))
        {
            ActionEntityCreateSoftBody();
        }
        else if (ImGui::MenuItem("Collider"))
        {
            ActionEntityCreateCollider();
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

    // ENVIRONMENT
    if (ImGui::BeginMenu("Environment"))
    {
        if (ImGui::MenuItem("Environment"))
        {
            ActionEntityCreateSkybox();
        }

        ImGui::EndMenu();
    }

    // TERRAIN
    if (ImGui::MenuItem("Terrain"))
    {
        ActionEntityCreateTerrain();
    }

    // PROBE
    if (ImGui::BeginMenu("Probe"))
    {
        if (ImGui::MenuItem("Reflection Probe"))
        {
            ActionEntityCreateReflectionProbe();
        }

        ImGui::EndMenu();
    }

    ImGui::EndPopup();
}

void WorldViewer::PopupEntityRename() const
{
    if (_Widget_World::g_popupRenameentity)
    {
        ImGui::OpenPopup("##RenameEntity");
        _Widget_World::g_popupRenameentity = false;
    }

    if (ImGui::BeginPopup("##RenameEntity"))
    {
        auto selectedentity = EditorHelper::Get().g_selected_entity.lock();
        if (!selectedentity)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        auto name = selectedentity->GetName();

        ImGui::Text("Name:");
        ImGui::InputText("##edit", &name);
        selectedentity->SetName(string(name));

        if (imgui_extension::button("Ok"))
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
    if (_Widget_World::g_input->GetKey(KeyCode::Delete))
    {
        ActionEntityDelete(EditorHelper::Get().g_selected_entity.lock());
    }

    // Save: Ctrl + S
    if (_Widget_World::g_input->GetKey(KeyCode::Ctrl_Left) && _Widget_World::g_input->GetKeyDown(KeyCode::S))
    {
        const string& file_path = _Widget_World::g_world->GetFilePath();

        if (file_path.empty())
        {
            m_editor->GetWidget<MenuBar>()->ShowWorldSaveDialog();
        }
        else
        {
            EditorHelper::Get().SaveWorld(_Widget_World::g_world->GetFilePath());
        }
    }

    // Load: Ctrl + L
    if (_Widget_World::g_input->GetKey(KeyCode::Ctrl_Left) && _Widget_World::g_input->GetKeyDown(KeyCode::L))
    {
        m_editor->GetWidget<MenuBar>()->ShowWorldLoadDialog();
    }
}

void WorldViewer::ActionEntityDelete(const shared_ptr<Entity>& entity)
{
    _Widget_World::g_world->RemoveEntity(entity.get());
}

Entity* WorldViewer::ActionEntityCreateEmpty()
{
    shared_ptr<Entity> entity = _Widget_World::g_world->CreateEntity();
    if (const shared_ptr<Entity> selected_entity = EditorHelper::Get().g_selected_entity.lock())
    {
        entity->GetTransform()->SetParent(selected_entity->GetTransform());
    }

    return entity.get();
}

void WorldViewer::ActionEntityCreateCube()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Renderable>();
    renderable->SetGeometry(DefaultGeometry::Cube);
    renderable->SetDefaultMaterial();
    entity->SetName("Cube");
}

void WorldViewer::ActionEntityCreateQuad()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Renderable>();
    renderable->SetGeometry(DefaultGeometry::Quad);
    renderable->SetDefaultMaterial();
    entity->SetName("Quad");
}

void WorldViewer::ActionEntityCreateSphere()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Renderable>();
    renderable->SetGeometry(DefaultGeometry::Sphere);
    renderable->SetDefaultMaterial();
    entity->SetName("Sphere");
}

void WorldViewer::ActionEntityCreateCylinder()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Renderable>();
    renderable->SetGeometry(DefaultGeometry::Cylinder);
    renderable->SetDefaultMaterial();
    entity->SetName("Cylinder");
}

void WorldViewer::ActionEntityCreateCone()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Renderable>();
    renderable->SetGeometry(DefaultGeometry::Cone);
    renderable->SetDefaultMaterial();
    entity->SetName("Cone");
}

void WorldViewer::ActionEntityCreateCamera()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Camera>();
    entity->SetName("Camera");
}

void WorldViewer::ActionEntityCreateTerrain()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Terrain>();
    entity->SetName("Terrain");
}

void WorldViewer::ActionEntityCreateLightDirectional()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Light>()->SetLightType(LightType::Directional);
    entity->SetName("Directional");
}

void WorldViewer::ActionEntityCreateLightPoint()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetName("Point");

    Light* light = entity->AddComponent<Light>();
    light->SetLightType(LightType::Point);
    light->SetIntensity(LightIntensity::bulb_150_watt);
}

void WorldViewer::ActionEntityCreateLightSpot()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetName("Spot");

    Light* light = entity->AddComponent<Light>();
    light->SetLightType(LightType::Spot);
    light->SetIntensity(LightIntensity::bulb_150_watt);
}

void WorldViewer::ActionEntityCreateRigidBody()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<RigidBody>();
    entity->SetName("RigidBody");
}

void WorldViewer::ActionEntityCreateSoftBody()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<SoftBody>();
    entity->SetName("SoftBody");
}

void WorldViewer::ActionEntityCreateCollider()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Collider>();
    entity->SetName("Collider");
}

void WorldViewer::ActionEntityCreateConstraint()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Constraint>();
    entity->SetName("Constraint");
}

void WorldViewer::ActionEntityCreateAudioSource()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<AudioSource>();
    entity->SetName("AudioSource");
}

void WorldViewer::ActionEntityCreateAudioListener()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<AudioListener>();
    entity->SetName("AudioListener");
}

void WorldViewer::ActionEntityCreateSkybox()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Environment>();
    entity->SetName("Environment");
}

void WorldViewer::ActionEntityCreateReflectionProbe()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<ReflectionProbe>();
    entity->SetName("ReflectionProbe");
}
