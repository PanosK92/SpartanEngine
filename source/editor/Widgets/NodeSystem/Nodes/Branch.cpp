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
#include "Branch.h"
//===================================

namespace Node
{
    // Branch
    Branch::Branch(NodeId id, PinId& next_pin_id) : NodeBase(id, "Branch")
    {
        m_flow_in_id = next_pin_id++;
        m_condition_id = next_pin_id++;
        m_flow_true_id = next_pin_id++;
        m_flow_false_id = next_pin_id++;
        
        AddInput(m_flow_in_id, "", PinType::Flow);
        AddInput(m_condition_id, "Condition", PinType::Bool);
        AddOutput(m_flow_true_id, "True", PinType::Flow);
        AddOutput(m_flow_false_id, "False", PinType::Flow);
        
        SetType(NodeType::Blueprint);
    }

    void Branch::Execute()
    {
        bool condition = GetInputValue<bool>(1); // Index 1 is the condition pin
        // Flow execution logic would be handled by the node graph executor
        // For now, just store the condition result
        SetOutputValue<bool>(0, condition);   // True output
        SetOutputValue<bool>(1, !condition);  // False output
    }

}
