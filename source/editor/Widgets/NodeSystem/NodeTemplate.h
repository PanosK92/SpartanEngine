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
#include "NodeTypes.h"
#include <functional>
#include <string>
//=================

class NodeBase;

// Factory function type for creating nodes
typedef std::function<NodeBase*(NodeId, PinId&)> NodeFactory;

/**
 * @class NodeTemplate
 * @brief Represents a template for creating nodes in the node system.
 * This class encapsulates the properties and factory function needed to instantiate nodes of a specific type.
 *
 * @note - Copy operations are deleted to prevent ambiguity in node identity and ownership.
 * @note - Move operations are allowed to facilitate ownership transfer.
 */
class NodeTemplate
{
public:
    NodeTemplate(std::string name, NodeCategory category, NodeFactory factory);
    ~NodeTemplate() = default;

    // Copy operations
    NodeTemplate(const NodeTemplate&)                = delete;
    NodeTemplate& operator=(const NodeTemplate&)     = delete;

    // Move operations
    NodeTemplate(NodeTemplate&&) noexcept            = default;
    NodeTemplate& operator=(NodeTemplate&&) noexcept = default;

    [[nodiscard]] NodeBase* CreateNode(NodeId id, PinId& next_pin_id) const;
    [[nodiscard]] const std::string& GetName() const { return m_name; }
    [[nodiscard]] NodeCategory GetCategory() const { return m_category; }

private:
    std::string m_name;
    NodeCategory m_category;
    NodeFactory m_factory;
};
