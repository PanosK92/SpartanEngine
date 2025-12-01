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
#include "Pin.h"
#include "Resource/ResourceCache.h"
#include "../../ImGui/ImGui_Extension.h"
//===================================


Pin::Pin(int id, const char* name, PinType type) : m_id(id), m_node(nullptr), m_name(name), m_type(type), m_kind(PinKind::Input)
{   
}

void Pin::StartPin(NodeEditor::PinId id, NodeEditor::PinKind kind)
{
    BeginPin(id, kind);
}

void Pin::EndPin()
{
    NodeEditor::EndPin();
}

bool Pin::CanCreateLink(Pin* a, Pin* b)
{
    if (!a || !b || a == b || a->m_kind == b->m_kind || a->m_type != b->m_type || a->m_node == b->m_node)
        return false;

    return true;
}

ImColor Pin::GetIconColor(PinType type)
{
    switch (type)
    {
        default:
        case PinType::Flow: return {255, 255, 255};
        case PinType::Bool: return {220, 48, 48};
        case PinType::Int: return {68, 201, 156};
        case PinType::Float: return {147, 226, 74};
        case PinType::String: return {124, 21, 153};
        case PinType::Object: return {51, 150, 215};
        case PinType::Function: return {218, 0, 183};
        case PinType::Delegate: return {255, 48, 48};
    }
};

void Pin::DrawPinIcon(const Pin& pin, bool connected, int alpha)
{
    spartan::IconType iconType;
    ImColor color = GetIconColor(pin.m_type);
    color.Value.w = alpha / 255.0f;
    switch (pin.m_type)
    {
        case PinType::Flow: iconType = spartan::IconType::Flow; break;
        case PinType::Bool: iconType = spartan::IconType::Circle; break;
        case PinType::Int: iconType = spartan::IconType::Circle; break;
        case PinType::Float: iconType = spartan::IconType::Circle; break;
        case PinType::String: iconType = spartan::IconType::Circle; break;
        case PinType::Object: iconType = spartan::IconType::Circle; break;
        case PinType::Function: iconType = spartan::IconType::Circle; break;
        case PinType::Delegate: iconType = spartan::IconType::Square; break;
        default: return;
    }

    const float icon_size = 24.0f;
    ImVec4 tint = ImVec4(color.Value.x, color.Value.y, color.Value.z, color.Value.w);
    
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - icon_size * 0.25f);
    ImGuiSp::image(iconType, icon_size, tint);
}
