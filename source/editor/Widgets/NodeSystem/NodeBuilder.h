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
#include "Node.h"
#include "Pin.h"
#include <vector>
//=================

class NodeBuilder
{
public:
    NodeBuilder();

    bool IsPinLinked(NodeEditor::PinId id);
    void BuildNode(Node* node);
    void BuildNodes();

    Link* FindLink(NodeEditor::LinkId id);
    Node* FindNode(NodeEditor::NodeId id);
    Pin* FindPin(NodeEditor::PinId id);
    NodeEditor::LinkId GetNextLinkId();

    Node* SpawnInputActionNode();
    Node* SpawnBranchNode();
    Node* SpawnDoNNode();
    Node* SpawnOutputActionNode();
    Node* SpawnPrintStringNode();
    Node* SpawnMessageNode();
    Node* SpawnSetTimerNode();
    Node* SpawnLessNode();
    Node* SpawnWeirdNode();
    Node* SpawnTraceByChannelNode();
    Node* SpawnTreeSequenceNode();
    Node* SpawnTreeTaskNode();
    Node* SpawnTreeTask2Node();
    Node* SpawnComment();

    std::vector<Node>& GetNodes() { return m_nodes; }
    std::vector<Link>& GetLinks() { return m_links; }

private:
    int m_next_id               = 1;
    const int m_pin_icon_size   = 24;
    std::vector<Node> m_nodes;
    std::vector<Link> m_links;
    int GetNextId();

    enum class Stage : uint8_t
    {
        Invalid,
        Begin,
        Header,
        Content,
        Input,
        Output,
        Middle,
        End
    };

    bool SetStage(Stage stage);

    ImTextureID header_texture_id;
    int header_texture_width;
    int header_texture_height;
    NodeEditor::NodeId current_node_id;
    Stage current_stage;
    ImU32 header_color;
    ImVec2 node_min;
    ImVec2 node_max;
    ImVec2 header_min;
    ImVec2 header_max;
    ImVec2 content_min;
    ImVec2 content_max;
    bool has_header;
};
