/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Widget_World.h"
#include "Widget_Properties.h"
#include "../ImGui_Extension.h"
#include "../ImGui/Source/imgui_stdlib.h"
#include "Resource/ProgressReport.h"
#include "Rendering/Model.h"
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
//=========================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================

namespace _Widget_World
{
    static World* g_world            = nullptr;
    static Input* g_input            = nullptr;
    static bool g_popupRenameentity    = false;
    static ImGuiEx::DragDropPayload g_payload;
    // entities in relation to mouse events
    static Entity* g_entity_copied    = nullptr;
    static Entity* g_entity_hovered    = nullptr;
    static Entity* g_entity_clicked    = nullptr;
}

Widget_World::Widget_World(Editor* editor) : Widget(editor)
{
    m_title                    = "World";
    _Widget_World::g_world    = m_context->GetSubsystem<World>();
    _Widget_World::g_input    = m_context->GetSubsystem<Input>();

    m_flags |= ImGuiWindowFlags_HorizontalScrollbar;

    // Subscribe to entity clicked engine event
    EditorHelper::Get().g_on_entity_selected = [this](){ SetSelectedEntity(EditorHelper::Get().g_selected_entity.lock(), false); };
}

void Widget_World::Tick()
{
    // If something is being loaded, don't parse the hierarchy
    auto& progress_report            = ProgressReport::Get();
    const auto is_loading_model        = progress_report.GetIsLoading(g_progress_model_importer);
    const auto is_loading_scene        = progress_report.GetIsLoading(g_progress_world);
    const auto is_loading            = is_loading_model || is_loading_scene;
    if (is_loading)
        return;
    
    TreeShow();

    // On left click, select entity but only on release
    if (ImGui::IsMouseReleased(0) && _Widget_World::g_entity_clicked)
    {
        // Make sure that the mouse was released while on the same entity
        if (_Widget_World::g_entity_hovered && _Widget_World::g_entity_hovered->GetId() == _Widget_World::g_entity_clicked->GetId())
        {
            SetSelectedEntity(_Widget_World::g_entity_clicked->GetPtrShared());
        }
        _Widget_World::g_entity_clicked = nullptr;
    }
}

void Widget_World::TreeShow()
{
    OnTreeBegin();

    if (ImGui::TreeNodeEx("Root", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Dropping on the scene node should unparent the entity
        if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayload_Entity))
        {
            const auto entity_id = get<unsigned int>(payload->data);
            if (const auto dropped_entity = _Widget_World::g_world->EntityGetById(entity_id))
            {
                dropped_entity->GetTransform()->SetParent(nullptr);
            }
        }

        auto rootentities = _Widget_World::g_world->EntityGetRoots();
        for (const auto& entity : rootentities)
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

void Widget_World::OnTreeBegin()
{
    _Widget_World::g_entity_hovered = nullptr;
}

void Widget_World::OnTreeEnd()
{
    HandleKeyShortcuts();
    HandleClicking();
    Popups();
}

void Widget_World::TreeAddEntity(Entity* entity)
{
    if (!entity)
        return;

    m_expanded_to_selection             = false;
    bool is_selected_entity             = false;
    const bool is_visible_in_hierarchy    = entity->IsVisibleInHierarchy();
    bool has_visible_children            = false;
   

    // Don't draw invisible entities
    if (!is_visible_in_hierarchy)
        return;

    // Determine children visibility
    auto children = entity->GetTransform()->GetChildren();
    for (const auto& child : children)
    {
        if (child->GetEntity()->IsVisibleInHierarchy())
        {
            has_visible_children = true;
            break;
        }
    }

    // Flags
    ImGuiTreeNodeFlags node_flags    = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;

    // Flag - Is expandable (has children) ?
    node_flags |= has_visible_children ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf; 

    // Flag - Is selected?
    if (const auto selected_entity = EditorHelper::Get().g_selected_entity.lock())
    {
        node_flags |= selected_entity->GetId() == entity->GetId() ? ImGuiTreeNodeFlags_Selected : node_flags;

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

    const bool is_node_open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(entity->GetId())), node_flags, entity->GetName().c_str());

    // Keep a copy of the selected item's rect so that we can scroll to bring it into view
    if ((node_flags & ImGuiTreeNodeFlags_Selected) && m_expand_to_selection)
    {
        m_selected_entity_rect = m_window->DC.LastItemRect;
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

void Widget_World::HandleClicking()
{
    const auto is_window_hovered    = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const auto left_click            = ImGui::IsMouseClicked(0);
    const auto right_click            = ImGui::IsMouseClicked(1);

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

void Widget_World::EntityHandleDragDrop(Entity* entity_ptr) const
{
    // Drag
    if (ImGui::BeginDragDropSource())
    {
        _Widget_World::g_payload.data = entity_ptr->GetId();
        _Widget_World::g_payload.type = ImGuiEx::DragPayload_Entity;
        ImGuiEx::CreateDragPayload(_Widget_World::g_payload);
        ImGui::EndDragDropSource();
    }
    // Drop
    if (auto payload = ImGuiEx::ReceiveDragPayload(ImGuiEx::DragPayload_Entity))
    {
        const auto entity_id = get<unsigned int>(payload->data);
        if (const auto dropped_entity = _Widget_World::g_world->EntityGetById(entity_id))
        {
            if (dropped_entity->GetId() != entity_ptr->GetId())
            {
                dropped_entity->GetTransform()->SetParent(entity_ptr->GetTransform());
            }
        }
    }
}

void Widget_World::SetSelectedEntity(const shared_ptr<Entity>& entity, const bool from_editor /*= true*/)
{
    m_expand_to_selection = true;

    // If the update comes from this widget, let the engine know about it
    if (from_editor)
    {
        EditorHelper::Get().SetSelectedEntity(entity);
    }

    Widget_Properties::Inspect(entity);
}

void Widget_World::Popups()
{
    PopupContextMenu();
    PopupEntityRename();
}

void Widget_World::PopupContextMenu() const
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

    ImGui::EndPopup();
}

void Widget_World::PopupEntityRename() const
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

        if (ImGui::Button("Ok")) 
        { 
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        ImGui::EndPopup();
    }
}

void Widget_World::HandleKeyShortcuts()
{
    if (_Widget_World::g_input->GetKey(KeyCode::Delete))
    {
        ActionEntityDelete(EditorHelper::Get().g_selected_entity.lock());
    }
}

void Widget_World::ActionEntityDelete(const shared_ptr<Entity>& entity)
{
    _Widget_World::g_world->EntityRemove(entity);
}

Entity* Widget_World::ActionEntityCreateEmpty()
{
    const auto entity = _Widget_World::g_world->EntityCreate().get();
    if (const auto selected_entity = EditorHelper::Get().g_selected_entity.lock())
    {
        entity->GetTransform()->SetParent(selected_entity->GetTransform());
    }

    return entity;
}

void Widget_World::ActionEntityCreateCube()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Renderable>();
    renderable->GeometrySet(Geometry_Default_Cube);
    renderable->UseDefaultMaterial();
    entity->SetName("Cube");
}

void Widget_World::ActionEntityCreateQuad()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Renderable>();
    renderable->GeometrySet(Geometry_Default_Quad);
    renderable->UseDefaultMaterial();
    entity->SetName("Quad");
}

void Widget_World::ActionEntityCreateSphere()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Renderable>();
    renderable->GeometrySet(Geometry_Default_Sphere);
    renderable->UseDefaultMaterial();
    entity->SetName("Sphere");
}

void Widget_World::ActionEntityCreateCylinder()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Renderable>();
    renderable->GeometrySet(Geometry_Default_Cylinder);
    renderable->UseDefaultMaterial();
    entity->SetName("Cylinder");
}

void Widget_World::ActionEntityCreateCone()
{
    auto entity = ActionEntityCreateEmpty();
    auto renderable = entity->AddComponent<Renderable>();
    renderable->GeometrySet(Geometry_Default_Cone);
    renderable->UseDefaultMaterial();
    entity->SetName("Cone");
}

void Widget_World::ActionEntityCreateCamera()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Camera>();
    entity->SetName("Camera");
}

void Widget_World::ActionEntityCreateTerrain()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Terrain>();
    entity->SetName("Terrain");
}

void Widget_World::ActionEntityCreateLightDirectional()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Light>()->SetLightType(LightType::Directional);
    entity->SetName("Directional");
}

void Widget_World::ActionEntityCreateLightPoint()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetName("Point");

    Light* light = entity->AddComponent<Light>();
    light->SetLightType(LightType::Point);
    light->SetIntensity(2600.0f); // your typical 150 watt light bulb
}

void Widget_World::ActionEntityCreateLightSpot()
{
    auto entity = ActionEntityCreateEmpty();
    entity->SetName("Spot");

    Light* light = entity->AddComponent<Light>();
    light->SetLightType(LightType::Spot);
    light->SetIntensity(2600.0f); // your typical 150 watt light bulb
}

void Widget_World::ActionEntityCreateRigidBody()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<RigidBody>();
    entity->SetName("RigidBody");
}

void Widget_World::ActionEntityCreateSoftBody()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<SoftBody>();
    entity->SetName("SoftBody");
}

void Widget_World::ActionEntityCreateCollider()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Collider>();
    entity->SetName("Collider");
}

void Widget_World::ActionEntityCreateConstraint()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Constraint>();
    entity->SetName("Constraint");
}

void Widget_World::ActionEntityCreateAudioSource()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<AudioSource>();
    entity->SetName("AudioSource");
}

void Widget_World::ActionEntityCreateAudioListener()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<AudioListener>();
    entity->SetName("AudioListener");
}

void Widget_World::ActionEntityCreateSkybox()
{
    auto entity = ActionEntityCreateEmpty();
    entity->AddComponent<Environment>();
    entity->SetName("Environment");
}
