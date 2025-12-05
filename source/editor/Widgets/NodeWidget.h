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

#pragma once

//= INCLUDES ======
#include "Widget.h"
#include "NodeSystem/NodeBuilder.h"
#include "NodeSystem/NodeTypes.h"
#include "../ImGui/ImGui_ViewGrid.h"
#include <memory>
#include <map>
//=================

class Pin;
class NodeTemplate;

class NodeWidget : public Widget
{
public:
    NodeWidget(Editor *editor);
    ~NodeWidget();

    void OnTick() override;
    void OnVisible() override;
    void OnTickVisible() override;

    Grid& GetGrid() { return m_grid; }
    [[nodiscard]] const Grid& GetGrid() const { return m_grid; }

private:
    void DrawNodes();
    void DrawNode(NodeBase* node);
    void DrawPin(const Pin& pin, const ImVec2& pos);
    void DrawLinks();
    void HandleInteractions();
    void ShowContextMenu();
    void ShowNodeCreationPopup();
    
    Pin* FindPinAt(const ImVec2& pos);
    NodeBase* FindNodeAt(const ImVec2& pos);
    Link* FindLinkNear(const ImVec2& pos, float max_distance = 10.0f);

    std::unique_ptr<NodeBuilder> m_node_builder;
    Grid m_grid;
    bool m_first_run = true;
    
    // Interaction state
    NodeBase* m_dragged_node = nullptr;
    ImVec2 m_drag_offset = ImVec2(0, 0);
    
    Pin* m_link_start_pin = nullptr;
    ImVec2 m_link_end_pos = ImVec2(0, 0);
    
    NodeBase* m_hovered_node = nullptr;
    Pin* m_hovered_pin = nullptr;
    Link* m_hovered_link = nullptr;
    
    // Context menu state
    bool m_show_create_menu = false;
    ImVec2 m_create_menu_pos = ImVec2(0, 0);
    Pin* m_create_from_pin = nullptr;
    
    // Visual settings
    const float m_node_rounding = 4.0f;
    const float m_node_padding = 8.0f;
    const float m_pin_radius = 6.0f;
    const int m_pin_icon_size = 24;
    
    // Category filter for node creation
    NodeCategory m_current_category = NodeCategory::Math;
    char m_search_buffer[256] = "";
};
