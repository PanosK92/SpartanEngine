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
#include "NodeBuilder.h"
#include "Node.h"
#include "NodeProperties.h"
#include "Pin.h"
#include "../../ImGui/Nodes/imgui_node_editor_utilities.h"
#include <algorithm>
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

NodeBuilder::NodeBuilder() : current_stage(Stage::Invalid) , has_header(false)
{
}

Node* NodeBuilder::FindNode(NodeEditor::NodeId id)
{
    for (auto& node : m_nodes)
        if (node.GetID() == id) return &node;

    return nullptr;
}

Link* NodeBuilder::FindLink(NodeEditor::LinkId id)
{
    for (auto& link : m_links)
        if (link.GetID() == id) return &link;

    return nullptr;
}

int NodeBuilder::GetNextId() { return m_next_id++; }

NodeEditor::LinkId NodeBuilder::GetNextLinkId() { return {static_cast<unsigned long long>(GetNextId())}; }

Pin* NodeBuilder::FindPin(NodeEditor::PinId id)
{
    if (!id) return nullptr;

    for (auto& node : m_nodes)
    {
        for (auto& pin : node.GetInputs())
            if (pin.GetID() == id) return &pin;

        for (auto& pin : node.GetOutputs())
            if (pin.GetID() == id) return &pin;
    }

    return nullptr;
}

bool NodeBuilder::SetStage(Stage stage)
{
    if (stage == current_stage) return false;

    auto oldStage = current_stage;
    current_stage  = stage;

    ImVec2 cursor;
    switch (oldStage)
    {
        case Stage::Begin: break;

        case Stage::Header:
            ImGui::EndHorizontal();
            header_min = ImGui::GetItemRectMin();
            header_max = ImGui::GetItemRectMax();

            // spacing between header and content
            ImGui::Spring(0, ImGui::GetStyle().ItemSpacing.y * 2.0f);

            break;

        case Stage::Content: break;

        case Stage::Input:
            NodeEditor::PopStyleVar(2);

            ImGui::Spring(1, 0);
            ImGui::EndVertical();

            // #debug
            // ImGui::GetWindowDrawList()->AddRect(
            //     ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 255));

            break;

        case Stage::Middle:
            ImGui::EndVertical();

            // #debug
            // ImGui::GetWindowDrawList()->AddRect(
            //     ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 255));

            break;

        case Stage::Output:
            NodeEditor::PopStyleVar(2);

            ImGui::Spring(1, 0);
            ImGui::EndVertical();

            // #debug
            // ImGui::GetWindowDrawList()->AddRect(
            //     ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 255));

            break;

        case Stage::End: break;

        case Stage::Invalid: break;
    }

    switch (stage)
    {
        case Stage::Begin: ImGui::BeginVertical("node"); break;

        case Stage::Header:
            has_header = true;

            ImGui::BeginHorizontal("header");
            break;

        case Stage::Content:
            if (oldStage == Stage::Begin) ImGui::Spring(0);

            ImGui::BeginHorizontal("content");
            ImGui::Spring(0, 0);
            break;

        case Stage::Input:
            ImGui::BeginVertical("inputs", ImVec2(0, 0), 0.0f);

            NodeEditor::PushStyleVar(NodeEditor::StyleVar_PivotAlignment, ImVec2(0, 0.5f));
            NodeEditor::PushStyleVar(NodeEditor::StyleVar_PivotSize, ImVec2(0, 0));

            if (!has_header) ImGui::Spring(1, 0);
            break;

        case Stage::Middle:
            ImGui::Spring(1);
            ImGui::BeginVertical("middle", ImVec2(0, 0), 1.0f);
            break;

        case Stage::Output:
            if (oldStage == Stage::Middle || oldStage == Stage::Input) ImGui::Spring(1);
            else
                ImGui::Spring(1, 0);
            ImGui::BeginVertical("outputs", ImVec2(0, 0), 1.0f);

            NodeEditor::PushStyleVar(NodeEditor::StyleVar_PivotAlignment, ImVec2(1.0f, 0.5f));
            NodeEditor::PushStyleVar(NodeEditor::StyleVar_PivotSize, ImVec2(0, 0));

            if (!has_header) ImGui::Spring(1, 0);
            break;

        case Stage::End:
            if (oldStage == Stage::Input) ImGui::Spring(1, 0);
            if (oldStage != Stage::Begin) ImGui::EndHorizontal();
            content_min = ImGui::GetItemRectMin();
            content_max = ImGui::GetItemRectMax();

            // ImGui::Spring(0);
            ImGui::EndVertical();
            node_min = ImGui::GetItemRectMin();
            node_max = ImGui::GetItemRectMax();
            break;

        case Stage::Invalid: break;
    }

    return true;
}

bool NodeBuilder::IsPinLinked(NodeEditor::PinId id)
{
    if (!id) return false;

    for (auto& link : m_links)
    {
        if (link.GetStartPinID() == id || link.GetEndPinID() == id)
            return true;
    }

    return false;
}

Link* NodeBuilder::CreateLink(NodeEditor::PinId startPinId, NodeEditor::PinId endPinId)
{
    NodeEditor::LinkId linkId = GetNextLinkId();
    m_links.emplace_back(linkId, startPinId, endPinId);
    return &m_links.back();
}

bool NodeBuilder::DeleteLink(NodeEditor::LinkId linkId)
{
    for (auto it = m_links.begin(); it != m_links.end(); ++it)
    {
        if (it->GetID() == linkId)
        {
            m_links.erase(it);
            return true;
        }
    }
    return false;
}

void NodeBuilder::ClearLinks()
{
    m_links.clear();
}

void NodeBuilder::BuildNode(Node* node)
{
    for (auto& input : node->GetInputs())
    {
        input.SetNode(node);
        input.SetKind(PinKind::Input);
    }

    for (auto& output : node->GetOutputs())
    {
        output.SetNode(node);
        output.SetKind(PinKind::Output);
    }
}

void NodeBuilder::BuildNodes()
{
    for (auto& node : m_nodes)
        BuildNode(&node);
}

Node* NodeBuilder::SpawnInputActionNode()
{
    m_nodes.emplace_back(GetNextId(), "InputAction Fire", ImColor(255, 128, 128));
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "", PinType::Delegate);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "Pressed", PinType::Flow);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "Released", PinType::Flow);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnBranchNode()
{
    m_nodes.emplace_back(GetNextId(), "Branch");
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "", PinType::Flow);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Condition", PinType::Bool);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "True", PinType::Flow);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "False", PinType::Flow);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnDoNNode()
{
    m_nodes.emplace_back(GetNextId(), "Do N");
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Enter", PinType::Flow);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "N", PinType::Int);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Reset", PinType::Flow);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "Exit", PinType::Flow);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "Counter", PinType::Int);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnOutputActionNode()
{
    m_nodes.emplace_back(GetNextId(), "OutputAction");
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Sample", PinType::Float);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "Condition", PinType::Bool);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Event", PinType::Delegate);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnPrintStringNode()
{
    m_nodes.emplace_back(GetNextId(), "Print String");
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "", PinType::Flow);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "In String", PinType::String);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "", PinType::Flow);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnMessageNode()
{
    m_nodes.emplace_back(GetNextId(), "", ImColor(128, 195, 248));
    m_nodes.back().SetType(NodeType::Simple);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "Message", PinType::String);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnSetTimerNode()
{
    m_nodes.emplace_back(GetNextId(), "Set Timer", ImColor(128, 195, 248));
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "", PinType::Flow);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Object", PinType::Object);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Function Name", PinType::Function);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Time", PinType::Float);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Looping", PinType::Bool);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "", PinType::Flow);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnLessNode()
{
    m_nodes.emplace_back(GetNextId(), "<", ImColor(128, 195, 248));
    m_nodes.back().SetType(NodeType::Simple);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "", PinType::Float);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "", PinType::Float);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "", PinType::Float);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnWeirdNode()
{
    m_nodes.emplace_back(GetNextId(), "o.O", ImColor(128, 195, 248));
    m_nodes.back().SetType(NodeType::Simple);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "", PinType::Float);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "", PinType::Float);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "", PinType::Float);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnTraceByChannelNode()
{
    m_nodes.emplace_back(GetNextId(), "Single Line Trace by Channel", ImColor(255, 128, 64));
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "", PinType::Flow);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Start", PinType::Flow);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "End", PinType::Int);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Trace Channel", PinType::Float);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Trace Complex", PinType::Bool);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Actors to Ignore", PinType::Int);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Draw Debug Type", PinType::Bool);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "Ignore Self", PinType::Bool);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "", PinType::Flow);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "Out Hit", PinType::Float);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "Return Value", PinType::Bool);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnTreeSequenceNode()
{
    m_nodes.emplace_back(GetNextId(), "Sequence");
    m_nodes.back().SetType(NodeType::Tree);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "", PinType::Flow);
    m_nodes.back().GetOutputs().emplace_back(GetNextId(), "", PinType::Flow);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnTreeTaskNode()
{
    m_nodes.emplace_back(GetNextId(), "Move To");
    m_nodes.back().SetType(NodeType::Tree);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "", PinType::Flow);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnTreeTask2Node()
{
    m_nodes.emplace_back(GetNextId(), "Random Wait");
    m_nodes.back().SetType(NodeType::Tree);
    m_nodes.back().GetInputs().emplace_back(GetNextId(), "", PinType::Flow);

    BuildNode(&m_nodes.back());

    return &m_nodes.back();
}

Node* NodeBuilder::SpawnComment()
{
    m_nodes.emplace_back(GetNextId(), "Test Comment");
    m_nodes.back().SetType(NodeType::Comment);
    m_nodes.back().SetSize(ImVec2(300, 200));

    return &m_nodes.back();
}

    /*
    Widgets::Icon(
    ImVec2(static_cast<float>(m_pin_icon_size), static_cast<float>(m_pin_icon_size)), iconType, connected, color,
    ImColor(32, 32, 32, alpha));*/
