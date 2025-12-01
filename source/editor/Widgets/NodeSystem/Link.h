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
#include "../../ImGui/Source/imgui_internal.h"
#include <vector>
//=================

namespace NodeEditor = ax::NodeEditor;

class Link
{
public:
    Link(NodeEditor::LinkId id, NodeEditor::PinId startPinId, NodeEditor::PinId endPinId);

    [[nodiscard]] NodeEditor::LinkId GetID() const { return m_id; }
    [[nodiscard]] NodeEditor::PinId GetStartPinID() const { return m_start_pin_id; }
    [[nodiscard]] NodeEditor::PinId GetEndPinID() const { return m_end_pin_id; }

private:
    NodeEditor::LinkId m_id;
    NodeEditor::PinId m_start_pin_id;
    NodeEditor::PinId m_end_pin_id;
    ImColor m_color;
};
