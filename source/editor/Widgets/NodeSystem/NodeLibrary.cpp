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
#include "NodeLibrary.h"
#include "Nodes/Addition.h"
#include "Nodes/Branch.h"
#include "Nodes/Division.h"
#include "Nodes/LessThan.h"
#include "Nodes/Multiplication.h"
#include "Nodes/Subtraction.h"
#include <algorithm>
//===================================

NodeLibrary& NodeLibrary::GetInstance()
{
    static NodeLibrary instance;
    return instance;
}

void NodeLibrary::Initialize()
{
    RegisterMathNodes();
    RegisterLogicNodes();
    RegisterUtilityNodes();
}

void NodeLibrary::RegisterTemplate(std::unique_ptr<NodeTemplate> node_template)
{
    m_templates.push_back(std::move(node_template));
}

std::vector<const NodeTemplate*> NodeLibrary::SearchTemplates(const std::string& search_text, NodeCategory category) const
{
    std::vector<const NodeTemplate*> results;
    
    for (const auto& tmpl : m_templates)
    {
        bool matches_category = (category == tmpl->GetCategory());
        bool matches_search = search_text.empty() || tmpl->GetName().find(search_text) != std::string::npos;
        
        if (matches_category && matches_search)
        {
            results.push_back(tmpl.get());
        }
    }
    
    return results;
}

void NodeLibrary::RegisterMathNodes()
{
    // Add node
    RegisterTemplate(std::make_unique<NodeTemplate>(
        "Add",
        NodeCategory::Math,
        [](NodeId id, PinId& next_pin_id) -> NodeBase* {
            return new Node::Addition(id, next_pin_id);
        }
    ));
    
    // Subtract node
    RegisterTemplate(std::make_unique<NodeTemplate>(
        "Subtract",
        NodeCategory::Math,
        [](NodeId id, PinId& next_pin_id) -> NodeBase* {
            return new Node::Subtraction(id, next_pin_id);
        }
    ));
    
    // Multiply node
    RegisterTemplate(std::make_unique<NodeTemplate>(
        "Multiply",
        NodeCategory::Math,
        [](NodeId id, PinId& next_pin_id) -> NodeBase* {
            return new Node::Multiplication(id, next_pin_id);
        }
    ));
    
    // Divide node
    RegisterTemplate(std::make_unique<NodeTemplate>(
        "Divide",
        NodeCategory::Math,
        [](NodeId id, PinId& next_pin_id) -> NodeBase* {
            return new Node::Division(id, next_pin_id);
        }
    ));
}

void NodeLibrary::RegisterLogicNodes()
{
    // Branch node
    RegisterTemplate(std::make_unique<NodeTemplate>(
        "Branch",
        NodeCategory::Logic,
        [](NodeId id, PinId& next_pin_id) -> NodeBase* {
            return new Node::Branch(id, next_pin_id);
        }
    ));
    
    // Less Than node
    RegisterTemplate(std::make_unique<NodeTemplate>(
        "Less Than",
        NodeCategory::Logic,
        [](NodeId id, PinId& next_pin_id) -> NodeBase* {
            return new Node::LessThan(id, next_pin_id);
        }
    ));
}

void NodeLibrary::RegisterUtilityNodes()
{
    // Future utility nodes can be added here
}
