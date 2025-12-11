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
#include "Addition.h"
//===================================

namespace Node
{
    Addition::Addition(NodeId id, PinId& next_pin_id) : NodeBase(id, "Add")
    {
        m_input_a_id = next_pin_id++;
        m_input_b_id = next_pin_id++;
        m_output_id = next_pin_id++;
        
        AddInput(m_input_a_id, "A", PinType::Float);
        AddInput(m_input_b_id, "B", PinType::Float);
        AddOutput(m_output_id, "Result", PinType::Float);
        
        SetType(NodeType::Simple);
    }

    void Addition::Execute()
    {
        float a = GetInputValue<float>(0);
        float b = GetInputValue<float>(1);
        float result = a + b;
        SetOutputValue<float>(0, result);
    }
}
