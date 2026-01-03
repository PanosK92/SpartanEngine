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

#pragma once

//= INCLUDES ======
#include "NodeTypes.h"
#include "../../ImGui/Source/imgui.h"
#include <string>
//=================

class NodeBase;

/**
 * @class Pin
 * @brief Represents a pin in the node system.
 * Pins are connection points on nodes that allow data or flow to be passed between nodes.
 *
 * @note - Pins have unique IDs (m_id) and are associated with a specific node (m_node).
 */
class Pin
{
public:
    Pin(PinId id, const char* name, PinType type, PinKind kind);

    // Default copy and move operations are appropriate
    Pin(const Pin&)                = default;
    Pin& operator=(const Pin&)     = default;
    Pin(Pin&&) noexcept            = default;
    Pin& operator=(Pin&&) noexcept = default;
    ~Pin()                         = default;

    static bool CanCreateLink(Pin* a, Pin* b);
    static ImColor GetIconColor(PinType type);
    void DrawIcon(bool connected, int alpha) const;

    [[nodiscard]] PinId GetID() const { return m_id; }
    [[nodiscard]] const std::string& GetName() const { return m_name; }
    [[nodiscard]] NodeBase* GetNode() const { return m_node; }
    [[nodiscard]] PinKind GetKind() const { return m_kind; }
    [[nodiscard]] PinType GetType() const { return m_type; }
    [[nodiscard]] bool IsLinked() const { return m_is_linked; }

    void SetNode(NodeBase* node) { m_node = node; }
    void SetLinked(bool linked) { m_is_linked = linked; }

private:
    PinId m_id;
    NodeBase* m_node;
    std::string m_name;
    PinType m_type;
    PinKind m_kind;
    bool m_is_linked = false;
};
