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
#include "NodeBase.h"
#include "Link.h"
#include "NodeTemplate.h"
#include <vector>
#include <memory>
//=================

class NodeBuilder
{
public:
    NodeBuilder();
    ~NodeBuilder() = default;

    // NodeBase management
    NodeBase* CreateNode(const NodeTemplate* node_template);
    NodeBase* CreateNode(NodeId id, const char* name);
    bool DeleteNode(NodeId nodeId);
    NodeBase* FindNode(NodeId id);
    const NodeBase* FindNode(NodeId id) const;

    // Pin management
    Pin* FindPin(PinId id);
    const Pin* FindPin(PinId id) const;
    bool IsPinLinked(PinId id) const;

    // Link management
    Link* CreateLink(PinId startPinId, PinId endPinId);
    bool DeleteLink(LinkId linkId);
    Link* FindLink(LinkId id);
    const Link* FindLink(LinkId id) const;
    void ClearLinks();

    // Accessors
    std::vector<std::unique_ptr<NodeBase>>& GetNodes() { return m_nodes; }
    const std::vector<std::unique_ptr<NodeBase>>& GetNodes() const { return m_nodes; }
    std::vector<std::unique_ptr<Link>>& GetLinks() { return m_links; }
    const std::vector<std::unique_ptr<Link>>& GetLinks() const { return m_links; }

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
