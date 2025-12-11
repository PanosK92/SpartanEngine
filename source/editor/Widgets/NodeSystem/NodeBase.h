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

/**
 * @brief Variant type for pin values, allowing for different data types to be stored.
 *
 * This variant can hold:
 * - std::monostate: Represents an uninitialized or empty state.
 * - bool: Boolean values.
 * - int: Integer values.
 * - float: Floating-point values.
 * - std::string: String values.
 * @note Additional types can be added as needed to support more complex data types.
 *
 * Usage example:
 * @code
 * PinValue value = 42; // Storing an integer
 * value = true; // Storing a boolean
 * value = std::string("Hello, World!"); // Storing a string
 * @endcode
 */
using PinValue = std::variant<std::monostate, bool, int, float, std::string>;

/**
 * @class NodeBase
 * @brief Base class for all nodes in the node system.
 * This class provides common functionality for managing inputs, outputs,
 * and execution logic of nodes.
 *
 * Derived classes should implement the Execute() method to define
 * the specific behavior of the node.
 *
 * @note Nodes can have multiple input and output pins,
 * and pin values can be accessed and modified using type-safe methods.
 */
class NodeBase
{
public:
    NodeBase(NodeId id, const char* name, ImColor color = ImColor(255, 255, 255));
    virtual ~NodeBase() = default;

    /**
     * @note Nodes have unique IDs (m_id) and are managed through std::unique_ptr in NodeBuilder.
     * Copying a node would create ambiguity about identity and ownership, so copy operations should be explicitly deleted.
     */
    NodeBase(const NodeBase&)                = delete;
    NodeBase& operator=(const NodeBase&)     = delete;

    /**
    * @note Moving a node is allowed to transfer ownership without duplicating the node.
    * Moving is safe and useful for container operations (like vector reallocation). 
    */
    NodeBase(NodeBase&&) noexcept            = default;
    NodeBase& operator=(NodeBase&&) noexcept = default;

    /**
     * @brief Executes the node's logic.
     * Derived classes should override this method to implement
     * the specific behavior of the node.
     * 
     * @note This is a virtual method with an empty default implementation.
     */
    virtual void Execute() {}

    void AddInput(PinId pin_id, const char* name, PinType type);
    void AddOutput(PinId pin_id, const char* name, PinType type);
    
    Pin* FindPin(PinId id);
    const Pin* FindPin(PinId id) const;

    // Pin value access
    void SetPinValue(PinId pin_id, const PinValue& value);
    PinValue GetPinValue(PinId pin_id) const;

    /**
     * @brief Sets the value of an input pin at the specified index.
     * @tparam T The type of the value to set.
     * @param input_index The index of the input pin.
     * @param value The value to set for the input pin.
     *
     * This function checks if the input index is valid and sets the value
     * of the corresponding input pin in the internal pin values map.
     *
     * @note Template helper is a type-safe value access
     */
    template<typename T>
    void SetInputValue(size_t input_index, const T& value)
    {
        if (input_index < m_inputs.size())
            m_pin_values[m_inputs[input_index].GetID()] = value;
    }

    /**
     * @brief Gets the value of an input pin at the specified index.
     * @tparam T The expected type of the value to retrieve.
     * @param input_index The index of the input pin.
     * @return The value of the input pin, or a default-constructed value if the index is invalid or the type does not match.
     */
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

    /**
     * @brief Sets the value of an output pin at the specified index.
     * @tparam T The type of the value to set.
     * @param output_index The index of the output pin.
     * @param value The value to set for the output pin.
     */
    template<typename T>
    void SetOutputValue(size_t output_index, const T& value)
    {
        if (output_index < m_outputs.size())
            m_pin_values[m_outputs[output_index].GetID()] = value;
    }

    /**
     * @brief Gets the value of an output pin at the specified index.
     * @tparam T The expected type of the value to retrieve.
     * @param output_index The index of the output pin.
     *
     * @return The value of the output pin, or a default-constructed value if the index is invalid or the type does not match.
     */
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

    // Inputs and outputs
    std::vector<Pin>& GetInputs() { return m_inputs; }
    [[nodiscard]] const std::vector<Pin>& GetInputs() const { return m_inputs; }
    std::vector<Pin>& GetOutputs() { return m_outputs; }
    [[nodiscard]] const std::vector<Pin>& GetOutputs() const { return m_outputs; }

    // Setters
    void SetType(NodeType type) { m_type = type; }
    void SetSize(const ImVec2& size) { m_size = size; }
    void SetColor(ImColor color) { m_color = color; }
    void SetPosition(const ImVec2& pos) { m_position = pos; }
    void SetSelected(bool selected) { m_selected = selected; }
    void SetDragging(bool dragging) { m_dragging = dragging; }

    [[nodiscard]] bool ContainsPoint(const ImVec2& point) const;
    [[nodiscard]] ImRect GetRect() const;

protected:
    NodeId m_id;
    std::string m_name;
    std::vector<Pin> m_inputs;
    std::vector<Pin> m_outputs;
    ImColor m_color;
    NodeType m_type;
    ImVec2 m_size       = ImVec2(0, 0);
    ImVec2 m_position   = ImVec2(0, 0);
    bool m_selected     = false;
    bool m_dragging     = false;
    
    // Pin values storage
    std::map<PinId, PinValue> m_pin_values;
};
