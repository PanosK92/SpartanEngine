/*
Copyright(c) 2015-2025 Panos Karabelas & Thomas Ray

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

//= INCLUDES ========================
#include "pch.h"
#include "NodeWidget.h"
#include "NodeSystem/NodeProperties.h"
#include "NodeSystem/Pin.h"
#include "NodeSystem/NodeLibrary.h"
#include "../ImGui/ImGui_Extension.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

NodeWidget::NodeWidget(Editor* editor) : Widget(editor)
{
    m_title = "Node Editor";
    m_flags = ImGuiWindowFlags_NoScrollbar;
    m_visible = false;
    m_alpha = 1.0f;
    m_node_builder = std::make_unique<NodeBuilder>();
    m_grid.SetWidgetContext(this);
}

NodeWidget::~NodeWidget() = default;

void NodeWidget::OnTick()
{
}

void NodeWidget::OnVisible()
{
    if (!m_first_run)
        return;

    // Initialize node library
    NodeLibrary::GetInstance().Initialize();
    
    // Create some initial test nodes
    {
        if (auto tmpl = NodeLibrary::GetInstance().SearchTemplates("Add", NodeCategory::Math); !tmpl.empty())
        {
            if (NodeBase* node = m_node_builder->CreateNode(tmpl[0]))
                node->SetPosition(ImVec2(100, 100));
        }
    }
    
    {
        if (auto tmpl = NodeLibrary::GetInstance().SearchTemplates("Multiply", NodeCategory::Math); !tmpl.empty())
        {
            if (NodeBase* node = m_node_builder->CreateNode(tmpl[0]))
                node->SetPosition(ImVec2(400, 150));
        }
    }

    m_first_run = false;
}

void NodeWidget::OnTickVisible()
{
    m_grid.Draw();         // Draw grid
    m_grid.HandleInput();  // Handle grid input (pan/zoom)
    
    DrawLinks();
    DrawNodes();

    HandleInteractions();
    ShowContextMenu();
    ShowNodeCreationPopup();
}

void NodeWidget::DrawNodes()
{
    for (const auto& node : m_node_builder->GetNodes())
    {
        DrawNode(node.get());
    }
}

void NodeWidget::DrawNode(NodeBase* node)
{
    if (!node)
        return;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 node_screen_pos = m_grid.GridToScreen(node->GetPosition());
    
    // Calculate node size if not set
    ImVec2 node_size = node->GetSize();
    if (node_size.x == 0 || node_size.y == 0)
    {
        // Calculate based on content
        constexpr float header_height = 30.0f;
        constexpr float pin_height = 24.0f;
        constexpr float min_width = 120.0f;
        const int max_pins = ImMax(node->GetInputs().size(), node->GetOutputs().size());
        
        node_size.x = min_width;
        node_size.y = header_height + pin_height * max_pins + m_node_padding * 2;
        node->SetSize(node_size);
    }
    
    // Draw node background
    ImU32 node_bg_color = node->IsSelected() ? IM_COL32(90, 90, 120, 255) : IM_COL32(75, 75, 75, 255);
    ImU32 node_border_color = node->IsSelected() ? IM_COL32(150, 150, 200, 255) : IM_COL32(32, 32, 32, 255);
    
    draw_list->AddRectFilled(
        node_screen_pos,
        ImVec2(node_screen_pos.x + node_size.x, node_screen_pos.y + node_size.y),
        node_bg_color,
        m_node_rounding);
    
    draw_list->AddRect(
        node_screen_pos,
        ImVec2(node_screen_pos.x + node_size.x, node_screen_pos.y + node_size.y),
        node_border_color,
        m_node_rounding,
        0,
        2.0f);
    
    // Draw header
    constexpr float header_height = 30.0f;
    ImU32 header_color = IM_COL32(60, 60, 60, 255);
    
    draw_list->AddRectFilled(
        node_screen_pos,
        ImVec2(node_screen_pos.x + node_size.x, node_screen_pos.y + header_height),
        header_color,
        m_node_rounding,
        ImDrawFlags_RoundCornersTop);
    
    // Draw node title
    ImVec2 text_pos = ImVec2(node_screen_pos.x + m_node_padding, node_screen_pos.y + m_node_padding);
    draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), node->GetName().c_str());
    
    // Draw pins
    float current_y = node_screen_pos.y + header_height + m_node_padding;
    constexpr float pin_spacing = 24.0f;
    
    // Draw input pins on the left
    for (const auto& pin : node->GetInputs())
    {
        ImVec2 pin_pos = ImVec2(node_screen_pos.x, current_y);
        DrawPin(pin, pin_pos);
        
        // Draw pin label
        ImVec2 label_pos = ImVec2(pin_pos.x + m_pin_radius * 3, pin_pos.y - 8);
        if (!pin.GetName().empty())
            draw_list->AddText(label_pos, IM_COL32(200, 200, 200, 255), pin.GetName().c_str());
        
        current_y += pin_spacing;
    }
    
    // Draw output pins on the right
    current_y = node_screen_pos.y + header_height + m_node_padding;
    for (const auto& pin : node->GetOutputs())
    {
        ImVec2 pin_pos = ImVec2(node_screen_pos.x + node_size.x, current_y);
        DrawPin(pin, pin_pos);
        
        // Draw pin label (right-aligned)
        if (!pin.GetName().empty())
        {
            ImVec2 text_size = ImGui::CalcTextSize(pin.GetName().c_str());
            ImVec2 label_pos = ImVec2(pin_pos.x - m_pin_radius * 3 - text_size.x, pin_pos.y - 8);
            draw_list->AddText(label_pos, IM_COL32(200, 200, 200, 255), pin.GetName().c_str());
        }
        
        current_y += pin_spacing;
    }
}

void NodeWidget::DrawPin(const Pin& pin, const ImVec2& pos)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImColor pin_color = Pin::GetIconColor(pin.GetType());
    
    const bool is_linked = pin.IsLinked();
    const float radius = m_pin_radius;
    
    // Draw pin circle
    if (is_linked)
    {
        draw_list->AddCircleFilled(pos, radius, pin_color);
    }
    else
    {
        draw_list->AddCircle(pos, radius, pin_color, 12, 2.0f);
    }
    
    // Highlight if hovered
    if (m_hovered_pin == &pin)
    {
        draw_list->AddCircle(pos, radius + 2, IM_COL32(255, 255, 255, 255), 12, 2.0f);
    }
}

void NodeWidget::DrawLinks()
{
    for (const auto& link : m_node_builder->GetLinks())
    {
        const Pin* start_pin = m_node_builder->FindPin(link->GetStartPinID());
        const Pin* end_pin = m_node_builder->FindPin(link->GetEndPinID());
        
        if (!start_pin || !end_pin)
            continue;
        
        const NodeBase* start_node = start_pin->GetNode();
        const NodeBase* end_node = end_pin->GetNode();
        
        if (!start_node || !end_node)
            continue;
        
        // Calculate pin positions
        const ImVec2 start_node_screen_pos = m_grid.GridToScreen(start_node->GetPosition());
        const ImVec2 end_node_screen_pos = m_grid.GridToScreen(end_node->GetPosition());
        
        // Find pin index and calculate position
        constexpr float header_height = 30.0f;
        constexpr float pin_spacing = 24.0f;
        
        // Start pin (output, on right side)
        size_t start_pin_index = 0;
        for (size_t i = 0; i < start_node->GetOutputs().size(); ++i)
        {
            if (start_node->GetOutputs()[i].GetID() == start_pin->GetID())
            {
                start_pin_index = i;
                break;
            }
        }
        
        ImVec2 start_pos = ImVec2(
            start_node_screen_pos.x + start_node->GetSize().x,
            start_node_screen_pos.y + header_height + m_node_padding + start_pin_index * pin_spacing);
        
        // End pin (input, on left side)
        size_t end_pin_index = 0;
        for (size_t i = 0; i < end_node->GetInputs().size(); ++i)
        {
            if (end_node->GetInputs()[i].GetID() == end_pin->GetID())
            {
                end_pin_index = i;
                break;
            }
        }
        
        ImVec2 end_pos = ImVec2(
            end_node_screen_pos.x,
            end_node_screen_pos.y + header_height + m_node_padding + end_pin_index * pin_spacing);
        
        // Draw the link
        float thickness = (m_hovered_link == link.get()) ? 4.0f : 2.5f;
        link->Draw(start_pos, end_pos, link->GetColor(), thickness);
    }
    
    // Draw link being created
    if (m_link_start_pin)
    {
        if (const NodeBase* start_node = m_link_start_pin->GetNode())
        {
            const ImVec2 start_node_screen_pos = m_grid.GridToScreen(start_node->GetPosition());
            constexpr float header_height = 30.0f;
            constexpr float pin_spacing = 24.0f;
            
            // Find pin position
            ImVec2 start_pos;
            if (m_link_start_pin->GetKind() == PinKind::Output)
            {
                size_t pin_index = 0;
                for (size_t i = 0; i < start_node->GetOutputs().size(); ++i)
                {
                    if (start_node->GetOutputs()[i].GetID() == m_link_start_pin->GetID())
                    {
                        pin_index = i;
                        break;
                    }
                }
                start_pos = ImVec2(
                    start_node_screen_pos.x + start_node->GetSize().x,
                    start_node_screen_pos.y + header_height + m_node_padding + pin_index * pin_spacing);
            }
            else
            {
                size_t pin_index = 0;
                for (size_t i = 0; i < start_node->GetInputs().size(); ++i)
                {
                    if (start_node->GetInputs()[i].GetID() == m_link_start_pin->GetID())
                    {
                        pin_index = i;
                        break;
                    }
                }
                start_pos = ImVec2(
                    start_node_screen_pos.x,
                    start_node_screen_pos.y + header_height + m_node_padding + pin_index * pin_spacing);
            }
            
            // Draw temporary link to mouse
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const float distance = ImMax(ImAbs(m_link_end_pos.x - start_pos.x), 1.0f);
            const float offset = distance * 0.5f;
            
            ImVec2 cp0 = start_pos;
            ImVec2 cp1 = ImVec2(start_pos.x + offset, start_pos.y);
            ImVec2 cp2 = ImVec2(m_link_end_pos.x - offset, m_link_end_pos.y);
            ImVec2 cp3 = m_link_end_pos;
            
            draw_list->AddBezierCubic(cp0, cp1, cp2, cp3, Pin::GetIconColor(m_link_start_pin->GetType()), 2.5f);
        }
    }
}

void NodeWidget::HandleInteractions()
{
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse_pos = ImGui::GetMousePos();
    const ImVec2 grid_mouse_pos = m_grid.ScreenToGrid(mouse_pos);
    
    // Update hovered elements
    m_hovered_node = FindNodeAt(mouse_pos);
    m_hovered_pin = FindPinAt(mouse_pos);
    m_hovered_link = FindLinkNear(mouse_pos);
    
    // Handle node dragging
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_hovered_node && !m_hovered_pin && !io.KeyAlt)
    {
        m_dragged_node = m_hovered_node;
        m_drag_offset = ImVec2(
            grid_mouse_pos.x - m_hovered_node->GetPosition().x,
            grid_mouse_pos.y - m_hovered_node->GetPosition().y
        );
        m_dragged_node->SetSelected(true);
        m_dragged_node->SetDragging(true);
    }
    
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && m_dragged_node)
    {
        ImVec2 new_pos = ImVec2(grid_mouse_pos.x - m_drag_offset.x, grid_mouse_pos.y - m_drag_offset.y);
        m_dragged_node->SetPosition(new_pos);
    }
    
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && m_dragged_node)
    {
        m_dragged_node->SetDragging(false);
        m_dragged_node = nullptr;
    }
    
    // Handle link creation
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_hovered_pin && !m_link_start_pin)
    {
        m_link_start_pin = const_cast<Pin*>(m_hovered_pin);
        m_link_end_pos = mouse_pos;
    }
    
    if (m_link_start_pin)
    {
        m_link_end_pos = mouse_pos;
        
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (m_hovered_pin && m_hovered_pin != m_link_start_pin)
            {
                // Try to create link
                if (m_link_start_pin->GetKind() == PinKind::Output)
                    m_node_builder->CreateLink(m_link_start_pin->GetID(), m_hovered_pin->GetID());
                else
                    m_node_builder->CreateLink(m_hovered_pin->GetID(), m_link_start_pin->GetID());
            }
            m_link_start_pin = nullptr;
        }
    }
    
    // Handle link deletion (right-click on link)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && m_hovered_link)
    {
        m_node_builder->DeleteLink(m_hovered_link->GetID());
        m_hovered_link = nullptr;
    }
    
    // Handle selection
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_hovered_node && !m_hovered_pin && !io.KeyAlt)
    {
        // Deselect all
        for (auto& node : m_node_builder->GetNodes())
        {
            node->SetSelected(false);
        }
    }
    
    // Handle context menu
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !m_hovered_link)
    {
        m_show_create_menu = true;
        m_create_menu_pos = grid_mouse_pos;
        m_create_from_pin = const_cast<Pin*>(m_hovered_pin);
    }
}

void NodeWidget::ShowContextMenu()
{
    if (m_show_create_menu && !ImGui::IsPopupOpen("Create NodeBase"))
        ImGui::OpenPopup("Create NodeBase");
}

void NodeWidget::ShowNodeCreationPopup()
{
    if (ImGui::BeginPopup("Create NodeBase"))
    {
        ImGui::Text("Create NodeBase");
        ImGui::Separator();
        
        // Category tabs
        if (ImGui::BeginTabBar("NodeCategories"))
        {
            if (ImGui::BeginTabItem("Math"))
            {
                m_current_category = NodeCategory::Math;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Logic"))
            {
                m_current_category = NodeCategory::Logic;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Utility"))
            {
                m_current_category = NodeCategory::Utility;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        
        // Search box
        ImGui::InputText("Search", m_search_buffer, sizeof(m_search_buffer));

        ImGui::Separator();
        
        // List available templates
        for (auto templates = NodeLibrary::GetInstance().SearchTemplates(m_search_buffer, m_current_category);
            const auto* tmpl : templates)
        {
            if (ImGui::MenuItem(tmpl->GetName().c_str()))
            {
                if (NodeBase* new_node = m_node_builder->CreateNode(tmpl))
                {
                    new_node->SetPosition(m_create_menu_pos);
                    
                    // If created from a pin, try to auto-connect
                    if (m_create_from_pin)
                    {
                        if (m_create_from_pin->GetKind() == PinKind::Output && !new_node->GetInputs().empty())
                        {
                            m_node_builder->CreateLink(m_create_from_pin->GetID(), new_node->GetInputs()[0].GetID());
                        }
                        else if (m_create_from_pin->GetKind() == PinKind::Input && !new_node->GetOutputs().empty())
                        {
                            m_node_builder->CreateLink(new_node->GetOutputs()[0].GetID(), m_create_from_pin->GetID());
                        }
                    }
                }
                
                m_show_create_menu = false;
                m_create_from_pin = nullptr;
                ImGui::CloseCurrentPopup();
            }
        }
        
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_show_create_menu = false;
            m_create_from_pin = nullptr;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
    else if (!ImGui::IsPopupOpen("Create NodeBase"))
    {
        m_show_create_menu = false;
    }
}

Pin* NodeWidget::FindPinAt(const ImVec2& pos)
{
    const float pin_click_radius = m_pin_radius + 4.0f;
    
    for (const auto& node : m_node_builder->GetNodes())
    {
        constexpr float header_height = 30.0f;
        constexpr float pin_spacing  = 24.0f;
        const ImVec2 node_screen_pos = m_grid.GridToScreen(node->GetPosition());
        
        // Check input pins
        float current_y = node_screen_pos.y + header_height + m_node_padding;
        for (auto& pin : node->GetInputs())
        {
            ImVec2 pin_pos = ImVec2(node_screen_pos.x, current_y);
            if (float dist = sqrtf(powf(pos.x - pin_pos.x, 2) + powf(pos.y - pin_pos.y, 2)); dist <= pin_click_radius)
                return const_cast<Pin*>(&pin);
            current_y += pin_spacing;
        }
        
        // Check output pins
        current_y = node_screen_pos.y + header_height + m_node_padding;
        for (auto& pin : node->GetOutputs())
        {
            ImVec2 pin_pos = ImVec2(node_screen_pos.x + node->GetSize().x, current_y);
            if (float dist = sqrtf(powf(pos.x - pin_pos.x, 2) + powf(pos.y - pin_pos.y, 2)); dist <= pin_click_radius)
                return const_cast<Pin*>(&pin);
            current_y += pin_spacing;
        }
    }
    
    return nullptr;
}

NodeBase* NodeWidget::FindNodeAt(const ImVec2& pos)
{
    const ImVec2 grid_pos = m_grid.ScreenToGrid(pos);
    
    for (const auto& node : m_node_builder->GetNodes())
    {
        if (node->ContainsPoint(grid_pos))
            return node.get();
    }
    
    return nullptr;
}

Link* NodeWidget::FindLinkNear(const ImVec2& pos, float max_distance)
{
    // Simplified link hit detection - check distance to link midpoint
    for (const auto& link : m_node_builder->GetLinks())
    {
        const Pin* start_pin = m_node_builder->FindPin(link->GetStartPinID());
        const Pin* end_pin = m_node_builder->FindPin(link->GetEndPinID());
        
        if (!start_pin || !end_pin)
            continue;
        
        const NodeBase* start_node = start_pin->GetNode();
        const NodeBase* end_node = end_pin->GetNode();
        
        if (!start_node || !end_node)
            continue;
        
        const ImVec2 start_screen_pos = m_grid.GridToScreen(start_node->GetPosition());
        const ImVec2 end_screen_pos = m_grid.GridToScreen(end_node->GetPosition());
        
        ImVec2 midpoint = ImVec2(
            (start_screen_pos.x + end_screen_pos.x) * 0.5f,
            (start_screen_pos.y + end_screen_pos.y) * 0.5f
        );

        if (float dist = sqrtf(powf(pos.x - midpoint.x, 2) + powf(pos.y - midpoint.y, 2)); dist <= max_distance)
            return link.get();
    }
    
    return nullptr;
}
