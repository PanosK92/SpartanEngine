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
#include "NodeTypes.h"
#include "../../ImGui/Source/imgui.h"
#include "../../ImGui/Source/imgui_internal.h"
#include <string>
#include <vector>
#include <variant>
#include <map>
//=================

// Type-safe variant for pin values
using PinValue = std::variant<
    std::monostate,  // Empty/unset
    bool,
    int,
    float,
    std::string
>;

class NodeBase
{
public:
    NodeBase(NodeId id, const char* name, ImColor color = ImColor(255, 255, 255));
    virtual ~NodeBase() = default;

    // Execution - to be implemented by derived classes
    virtual void Execute() {}

    void AddInput(PinId pin_id, const char* name, PinType type);
    void AddOutput(PinId pin_id, const char* name, PinType type);
    
    Pin* FindPin(PinId id);
    const Pin* FindPin(PinId id) const;

    // Pin value access
    void SetPinValue(PinId pin_id, const PinValue& value);
    PinValue GetPinValue(PinId pin_id) const;
    
    // Template helpers for type-safe value access
    template<typename T>
    void SetInputValue(size_t input_index, const T& value)
    {
        if (input_index < m_inputs.size())
            m_pin_values[m_inputs[input_index].GetID()] = value;
    }
    
    template<typename T>
    T GetInputValue(size_t input_index) const
    {
        if (input_index < m_inputs.size())
        {
            auto it = m_pin_values.find(m_inputs[input_index].GetID());
            if (it != m_pin_values.end() && std::holds_alternative<T>(it->second))
                return std::get<T>(it->second);
        }
        return T{};
    }
    
    template<typename T>
    void SetOutputValue(size_t output_index, const T& value)
    {
        if (output_index < m_outputs.size())
            m_pin_values[m_outputs[output_index].GetID()] = value;
    }
    
    template<typename T>
    T GetOutputValue(size_t output_index) const
    {
        if (output_index < m_outputs.size())
        {
            auto it = m_pin_values.find(m_outputs[output_index].GetID());
            if (it != m_pin_values.end() && std::holds_alternative<T>(it->second))
                return std::get<T>(it->second);
        }
        return T{};
    }

    [[nodiscard]] NodeId GetID() const { return m_id; }
    [[nodiscard]] const std::string& GetName() const { return m_name; }
    [[nodiscard]] ImColor GetColor() const { return m_color; }
    [[nodiscard]] NodeType GetType() const { return m_type; }
    [[nodiscard]] ImVec2 GetSize() const { return m_size; }
    [[nodiscard]] ImVec2 GetPosition() const { return m_position; }
    [[nodiscard]] bool IsSelected() const { return m_selected; }
    [[nodiscard]] bool IsDragging() const { return m_dragging; }
    
    std::vector<Pin>& GetInputs() { return m_inputs; }
    const std::vector<Pin>& GetInputs() const { return m_inputs; }
    std::vector<Pin>& GetOutputs() { return m_outputs; }
    const std::vector<Pin>& GetOutputs() const { return m_outputs; }

    void SetType(NodeType type) { m_type = type; }
    void SetSize(const ImVec2& size) { m_size = size; }
    void SetColor(ImColor color) { m_color = color; }
    void SetPosition(const ImVec2& pos) { m_position = pos; }
    void SetSelected(bool selected) { m_selected = selected; }
    void SetDragging(bool dragging) { m_dragging = dragging; }

    bool ContainsPoint(const ImVec2& point) const;
    ImRect GetRect() const;

protected:
    NodeId m_id;
    std::string m_name;
    std::vector<Pin> m_inputs;
    std::vector<Pin> m_outputs;
    ImColor m_color;
    NodeType m_type;
    ImVec2 m_size = ImVec2(0, 0);
    ImVec2 m_position = ImVec2(0, 0);
    bool m_selected = false;
    bool m_dragging = false;
    
    // Pin values storage
    std::map<PinId, PinValue> m_pin_values;
};
