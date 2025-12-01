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
#include "../ImGui/ImGui_Extension.h"
#include "NodeSystem/NodeBuilder.h"
//=================

namespace ax::NodeEditor
{
    struct EditorContext;
}

struct LinkInfo
{
    NodeEditor::LinkId id;
    NodeEditor::PinId input_id;
    NodeEditor::PinId output_id;
};

class NodeWidget : public Widget
{
public:
    NodeWidget(Editor *editor);
    ~NodeWidget();

    void OnTick() override;
    void OnVisible() override;
    void OnTickVisible() override;

private:
    void TouchNode(NodeEditor::NodeId id);
    float GetTouchProgress(NodeEditor::NodeId id);
    void UpdateTouch();
    void DrawNodes();
    void DrawNode(Node* node);
    void DrawPinIcon(const Pin& pin, bool connected, int alpha);
    ImColor GetIconColor(PinType type);
    void HandleInteractions();
    void ShowContextMenu();

    std::unique_ptr<NodeBuilder> m_node_builder;
    NodeEditor::EditorContext* m_context    = nullptr;
    ImVector<LinkInfo> m_links; 
    int32_t m_index_displayed       = -1;
    bool m_first_run                = true;
    const float m_touch_time         = 1.0f;
    std::map<NodeEditor::NodeId, float, NodeIdLess> m_node_touch_time;
    
    // Context menu state
    NodeEditor::NodeId m_context_node_id = 0;
    NodeEditor::LinkId m_context_link_id = 0;
    NodeEditor::PinId m_context_pin_id = 0;
    bool m_create_new_node = false;
    Pin* m_new_node_link_pin = nullptr;
    Pin* m_new_link_pin = nullptr;
    int m_pin_icon_size = 24;
};
