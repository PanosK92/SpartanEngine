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
//=================

/**
 * @class Link
 * @brief Represents a connection (link) between two pins in the node system.
 *
 * This class encapsulates the properties and behaviors of a link, including its
 * unique identifier, the pins it connects, and its visual representation.
 *
 * @note Links are one-way connections. If you need bidirectional communication,
 * consider using two links or a different node setup.
 */
class Link
{
public:
    Link(LinkId id, PinId startPinId, PinId endPinId);
    ~Link() = default;

    Link(const Link&)                = delete;
    Link& operator=(const Link&)     = delete;
    Link(Link&&) noexcept            = default;
    Link& operator=(Link&&) noexcept = default;

    void Draw(const ImVec2& start_pos, const ImVec2& end_pos, ImColor color, float thickness = 3.0f) const;

    [[nodiscard]] LinkId GetID() const { return m_id; }
    [[nodiscard]] PinId GetStartPinID() const { return m_start_pin_id; }
    [[nodiscard]] PinId GetEndPinID() const { return m_end_pin_id; }
    [[nodiscard]] ImColor GetColor() const { return m_color; }

    void SetColor(ImColor color) { m_color = color; }

private:
    LinkId m_id;
    PinId m_start_pin_id;
    PinId m_end_pin_id;
    ImColor m_color = ImColor(255, 255, 255);
};
