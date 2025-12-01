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
#include "Pin.h"
#include "../../ImGui/Nodes/imgui_node_editor.h"
#include "../../ImGui/Source/imgui_internal.h"
#include <string>
#include <vector>
//=================

namespace NodeEditor = ax::NodeEditor;

enum class NodeType : uint8_t
{
    Blueprint,
    Simple,
    Tree,
    Comment,
};

class Node
{
public:
    Node(int id, const char* name, ImColor color = ImColor(255, 255, 255));

    [[nodiscard]] NodeEditor::NodeId GetID() const { return m_id; }
    [[nodiscard]] const std::string& GetName() const { return m_name; }
    [[nodiscard]] ImColor GetColor() const { return m_color; }
    std::vector<Pin>& GetInputs() { return m_inputs; }
    std::vector<Pin>& GetOutputs() { return m_outputs; }
    [[nodiscard]] NodeType GetType() const { return m_type; }
    [[nodiscard]] ImVec2 GetSize() const { return m_size; }
    std::string& GetState() { return m_state; }
    const std::string& GetState() const { return m_state; }

    void SetType(NodeType type) { m_type = type; }
    void SetSize(const ImVec2& size) { m_size = size; }
    void SetColor(ImColor color) { m_color = color; }

private:
    NodeEditor::NodeId m_id;
    std::string m_name;
    std::vector<Pin> m_inputs;
    std::vector<Pin> m_outputs;
    ImColor m_color;
    NodeType m_type;
    ImVec2 m_size;
    std::string m_state;
    std::string m_saved_state;
};

struct NodeIdLess
{
    bool operator()(const NodeEditor::NodeId& lhs, const NodeEditor::NodeId& rhs) const { return lhs.AsPointer() < rhs.AsPointer(); }
};
