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

//= INCLUDES ========================
#include "pch.h"
#include "NodeBase.h"
//===================================

NodeBase::NodeBase(NodeId id, const char* name, ImColor color) 
    : m_id(id), m_name(name), m_color(color), m_type(NodeType::Blueprint)
{
}

void NodeBase::AddInput(PinId pin_id, const char* name, PinType type)
{
    m_inputs.emplace_back(pin_id, name, type, PinKind::Input);
    m_inputs.back().SetNode(this);
}

void NodeBase::AddOutput(PinId pin_id, const char* name, PinType type)
{
    m_outputs.emplace_back(pin_id, name, type, PinKind::Output);
    m_outputs.back().SetNode(this);
}

Pin* NodeBase::FindPin(PinId id)
{
    for (auto& pin : m_inputs)
        if (pin.GetID() == id)
            return &pin;
    
    for (auto& pin : m_outputs)
        if (pin.GetID() == id)
            return &pin;
    
    return nullptr;
}

const Pin* NodeBase::FindPin(PinId id) const
{
    for (const auto& pin : m_inputs)
        if (pin.GetID() == id)
            return &pin;
    
    for (const auto& pin : m_outputs)
        if (pin.GetID() == id)
            return &pin;
    
    return nullptr;
}

void NodeBase::SetPinValue(PinId pin_id, const PinValue& value)
{
    m_pin_values[pin_id] = value;
}

PinValue NodeBase::GetPinValue(PinId pin_id) const
{
    auto it = m_pin_values.find(pin_id);
    if (it != m_pin_values.end())
        return it->second;
    return PinValue{};
}

bool NodeBase::ContainsPoint(const ImVec2& point) const
{
    ImRect rect = GetRect();
    return rect.Contains(point);
}

ImRect NodeBase::GetRect() const
{
    return ImRect(m_position, ImVec2(m_position.x + m_size.x, m_position.y + m_size.y));
}
