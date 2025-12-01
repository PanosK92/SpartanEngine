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
#include "../../ImGui/Nodes/imgui_node_editor.h"
//=================

class Node;
namespace NodeEditor = ax::NodeEditor;

enum class PinType : uint8_t
{
    Flow,
    Bool,
    Int,
    Float,
    String,
    Object,
    Function,
    Delegate,
};

enum class PinKind : uint8_t
{
    Output,
    Input
};

class Pin
{
public:
    Pin(int id, const char* name, PinType type);

    void StartPin(NodeEditor::PinId id, NodeEditor::PinKind kind);
    void EndPin();

    static bool CanCreateLink(Pin* a, Pin* b);
    ImColor GetIconColor(PinType type);
    void DrawPinIcon(const Pin& pin, bool connected, int alpha);

    [[nodiscard]] NodeEditor::PinId GetID() const { return m_id; }
    [[nodiscard]] const std::string& GetName() const { return m_name; }
    Node& GetNode() { return *m_node; }
    PinKind GetKind() const { return m_kind; }
    PinType GetType() const { return m_type; }

    void SetKind(PinKind kind) { m_kind = kind; }
    void SetNode(Node* node) { m_node = node; }

private:
    NodeEditor::PinId m_id;
    Node* m_node;
    std::string m_name;
    PinType m_type;
    PinKind m_kind;
};
