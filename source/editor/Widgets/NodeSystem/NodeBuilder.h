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
#include "Link.h"
#include "NodeBase.h"
#include "NodeTemplate.h"
#include "NodeTypes.h"
#include <memory>
#include <vector>
//=================

/**
 * @class NodeBuilder
 * @brief A class responsible for constructing and managing nodes and links in the node system.
 * This class provides functionality to create, delete, and find nodes and links,
 * as well as manage unique identifiers for nodes, pins, and links.
 *
 * @note Nodes and links are managed using std::unique_ptr to ensure proper memory management
 * and prevent memory leaks.
 */
class NodeBuilder
{
public:
    NodeBuilder();
    ~NodeBuilder() = default;

    NodeBuilder(const NodeBuilder&)                 = delete;
    NodeBuilder& operator=(const NodeBuilder&)      = delete;
    NodeBuilder(NodeBuilder&&) noexcept             = default;
    NodeBuilder& operator=(NodeBuilder&&) noexcept  = default;

    // NodeBase management
    NodeBase* CreateNode(const NodeTemplate* node_template);  // Creates a node based on a NodeTemplate
    NodeBase* CreateNode(NodeId id, const char* name);        // Creates a basic node with a given ID and name
    bool DeleteNode(NodeId nodeId);

    NodeBase* FindNode(NodeId id);
    [[nodiscard]] const NodeBase* FindNode(NodeId id) const;

    // Pin management
    Pin* FindPin(PinId id);
    [[nodiscard]] const Pin* FindPin(PinId id) const;
    [[nodiscard]] bool IsPinLinked(PinId id) const;

    // Link management
    Link* CreateLink(PinId startPinId, PinId endPinId);
    bool DeleteLink(LinkId linkId);
    Link* FindLink(LinkId id);
    [[nodiscard]] const Link* FindLink(LinkId id) const;
    void ClearLinks();

    // Accessors
    std::vector<std::unique_ptr<NodeBase>>& GetNodes() { return m_nodes; }
    [[nodiscard]] const std::vector<std::unique_ptr<NodeBase>>& GetNodes() const { return m_nodes; }
    std::vector<std::unique_ptr<Link>>& GetLinks() { return m_links; }
    [[nodiscard]] const std::vector<std::unique_ptr<Link>>& GetLinks() const { return m_links; }

    // ID generation
    NodeId GetNextNodeId() { return m_next_node_id++; }
    PinId GetNextPinId() { return m_next_pin_id++; }
    LinkId GetNextLinkId() { return m_next_link_id++; }

private:
    NodeId m_next_node_id = 1;
    PinId m_next_pin_id = 1;
    LinkId m_next_link_id = 1;
    
    std::vector<std::unique_ptr<NodeBase>> m_nodes;
    std::vector<std::unique_ptr<Link>> m_links;
};
