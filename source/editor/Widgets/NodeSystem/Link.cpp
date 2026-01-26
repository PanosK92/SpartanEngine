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
#include "Link.h"
#include "../../ImGui/Source/imgui.h"
//===================================

Link::Link(LinkId id, PinId startPinId, PinId endPinId)
    : m_id(id), m_start_pin_id(startPinId), m_end_pin_id(endPinId)
{
}

void Link::Draw(const ImVec2& start_pos, const ImVec2& end_pos, ImColor color, float thickness) const
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Calculate bezier control points for a smooth curve
    const float distance = (end_pos.x - start_pos.x > 0) ? (end_pos.x - start_pos.x) : -(end_pos.x - start_pos.x);
    const float offset = distance * 0.5f;
    
    ImVec2 cp0 = start_pos;
    ImVec2 cp1 = ImVec2(start_pos.x + offset, start_pos.y);
    ImVec2 cp2 = ImVec2(end_pos.x - offset, end_pos.y);
    ImVec2 cp3 = end_pos;
    
    draw_list->AddBezierCubic(cp0, cp1, cp2, cp3, color, thickness);
}
