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
#include "NodeBuilder.h"
#include "NodeBase.h"
#include "Pin.h"
#include "Link.h"
#include "NodeTemplate.h"
#include <algorithm>
//===================================

//= NAMESPACES ===============
using namespace std;
//============================

NodeBuilder::NodeBuilder() = default;

NodeBase* NodeBuilder::CreateNode(const NodeTemplate* node_template)
{
    if (!node_template)
        return nullptr;

    NodeId node_id = GetNextNodeId();
    
    // Use the template's factory to create the concrete node
    NodeBase* node = node_template->CreateNode(node_id, m_next_pin_id);
    
    if (!node)
        return nullptr;

    NodeBase* node_ptr = node;
    m_nodes.push_back(std::unique_ptr<NodeBase>(node));
    return node_ptr;
}

NodeBase* NodeBuilder::CreateNode(NodeId id, const char* name)
{
    // For custom nodes not from template, create a base instance
    // This is a fallback and shouldn't be used for nodes with logic
    auto node = std::make_unique<NodeBase>(id, name);
    NodeBase* node_ptr = node.get();
    m_nodes.push_back(std::move(node));
    return node_ptr;
}

bool NodeBuilder::DeleteNode(NodeId nodeId)
{

    if (auto it = ranges::find_if(m_nodes, [nodeId](const std::unique_ptr<NodeBase>& node)
    {
        return node->GetID() == nodeId;
    }); it != m_nodes.end())
    {
        // Remove all links connected to this node's pins
        std::vector<LinkId> links_to_remove;
        for (const auto& link : m_links)
        {
            Pin* start_pin = FindPin(link->GetStartPinID());
            Pin* end_pin = FindPin(link->GetEndPinID());
            
            if ((start_pin && start_pin->GetNode()->GetID() == nodeId) || (end_pin && end_pin->GetNode()->GetID() == nodeId))
            {
                links_to_remove.push_back(link->GetID());
            }
        }

        for (LinkId link_id : links_to_remove)
        {
            DeleteLink(link_id);
        }

        m_nodes.erase(it);
        return true;
    }

    return false;
}

NodeBase* NodeBuilder::FindNode(NodeId id)
{
    for (auto& node : m_nodes)
    {
        if (node->GetID() == id)
            return node.get();
    }
    return nullptr;
}

const NodeBase* NodeBuilder::FindNode(NodeId id) const
{
    for (const auto& node : m_nodes)
    {
        if (node->GetID() == id)
            return node.get();
    }
    return nullptr;
}

Pin* NodeBuilder::FindPin(PinId id)
{
    for (auto& node : m_nodes)
    {
        if (Pin* pin = node->FindPin(id))
            return pin;
    }
    return nullptr;
}

const Pin* NodeBuilder::FindPin(PinId id) const
{
    for (const auto& node : m_nodes)
    {
        if (const Pin* pin = node->FindPin(id))
            return pin;
    }
    return nullptr;
}

bool NodeBuilder::IsPinLinked(PinId id) const
{
    for (const auto& link : m_links)
    {
        if (link->GetStartPinID() == id || link->GetEndPinID() == id)
            return true;
    }
    return false;
}

Link* NodeBuilder::CreateLink(PinId startPinId, PinId endPinId)
{
    Pin* start_pin = FindPin(startPinId);
    Pin* end_pin = FindPin(endPinId);

    if (!start_pin || !end_pin || !Pin::CanCreateLink(start_pin, end_pin))
        return nullptr;

    LinkId link_id = GetNextLinkId();
    auto link = std::make_unique<Link>(link_id, startPinId, endPinId);
    
    // Set pin linked state
    start_pin->SetLinked(true);
    end_pin->SetLinked(true);
    
    // Set link color based on pin type
    link->SetColor(Pin::GetIconColor(start_pin->GetType()));

    Link* link_ptr = link.get();
    m_links.push_back(std::move(link));
    return link_ptr;
}

bool NodeBuilder::DeleteLink(LinkId linkId)
{
    auto it = ranges::find_if(m_links,
        [linkId](const std::unique_ptr<Link>& link) { return link->GetID() == linkId; });

    if (it != m_links.end())
    {
        // Update pin linked states
        Pin* start_pin = FindPin((*it)->GetStartPinID());
        Pin* end_pin = FindPin((*it)->GetEndPinID());
        
        if (start_pin && !IsPinLinked(start_pin->GetID()))
            start_pin->SetLinked(false);
        
        if (end_pin && !IsPinLinked(end_pin->GetID()))
            end_pin->SetLinked(false);

        m_links.erase(it);
        return true;
    }

    return false;
}

Link* NodeBuilder::FindLink(LinkId id)
{
    for (auto& link : m_links)
    {
        if (link->GetID() == id)
            return link.get();
    }
    return nullptr;
}

const Link* NodeBuilder::FindLink(LinkId id) const
{
    for (const auto& link : m_links)
    {
        if (link->GetID() == id)
            return link.get();
    }
    return nullptr;
}

void NodeBuilder::ClearLinks()
{
    // Update all pin linked states
    for (const auto& link : m_links)
    {
        Pin* start_pin = FindPin(link->GetStartPinID());
        Pin* end_pin = FindPin(link->GetEndPinID());
        
        if (start_pin)
            start_pin->SetLinked(false);
        if (end_pin)
            end_pin->SetLinked(false);
    }

    m_links.clear();
}
