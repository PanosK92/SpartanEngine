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
#include "NodeWidget.h"
#include "NodeSystem/NodeProperties.h"
#include "NodeSystem/Pin.h"
#include "../ImGui/Nodes/imgui_node_editor_utilities.h"
#include <ranges>
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    const float source_pane_vertical_split_percentage = 0.7f;
    const float source_pane_bottom_margin             = 30.0f;
}  // namespace

NodeWidget::NodeWidget(Editor* editor) : Widget(editor)
{
    m_title           = "Node Editor";
    m_flags           = ImGuiWindowFlags_NoScrollbar;
    m_visible         = false;
    m_alpha           = 1.0f;
    m_index_displayed = -1;
    m_node_builder    = std::make_unique<NodeBuilder>();

    m_grid.SetWidgetContext(this);
}

NodeWidget::~NodeWidget()
{
    if (m_context)
    {
        NodeEditor::DestroyEditor(m_context);
        m_context = nullptr;
    }
}

void NodeWidget::OnTick()
{
    UpdateTouch();
}

void NodeWidget::OnVisible()
{
    // Only create the editor context once
    if (m_context)
        return;

    NodeEditor::Config config;
    config.SettingsFile = "node_editor.json";
    config.UserPointer  = this;
    config.LoadNodeSettings = [](NodeEditor::NodeId nodeId, char* data, void* userPointer) -> size_t
    {
        auto self = static_cast<NodeWidget*>(userPointer);

        auto node = self->m_node_builder->FindNode(nodeId);
        if (!node) return 0;

        if (data != nullptr) memcpy(data, node->GetState().data(), node->GetState().size());
        return node->GetState().size();
    };

    config.SaveNodeSettings = [](NodeEditor::NodeId nodeId, const char* data, size_t size, NodeEditor::SaveReasonFlags reason, void* userPointer) -> bool
    {
        auto self = static_cast<NodeWidget*>(userPointer);

        auto node = self->m_node_builder->FindNode(nodeId);
        if (!node) return false;

        node->GetState().assign(data, size);
        self->TouchNode(nodeId);
        return true;
    };

    m_context = NodeEditor::CreateEditor(&config);
    NodeEditor::SetCurrentEditor(m_context);
    
    // Initialize nodes when the widget first becomes visible
    if (m_first_run)
    {
        // Create initial nodes
        Node* node;
        node = m_node_builder->SpawnInputActionNode();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(-252, 220));
        node = m_node_builder->SpawnBranchNode();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(-300, 351));
        node = m_node_builder->SpawnDoNNode();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(-238, 504));
        node = m_node_builder->SpawnOutputActionNode();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(71, 80));
        node = m_node_builder->SpawnSetTimerNode();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(168, 316));

        node = m_node_builder->SpawnTreeSequenceNode();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(1028, 329));
        node = m_node_builder->SpawnTreeTaskNode();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(1204, 458));
        node = m_node_builder->SpawnTreeTask2Node();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(868, 538));

        node = m_node_builder->SpawnComment();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(112, 576));
        NodeEditor::SetGroupSize(node->GetID(), ImVec2(384, 154));
        node = m_node_builder->SpawnComment();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(800, 224));
        NodeEditor::SetGroupSize(node->GetID(), ImVec2(640, 400));

        node = m_node_builder->SpawnLessNode();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(366, 652));
        node = m_node_builder->SpawnWeirdNode();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(144, 652));
        node = m_node_builder->SpawnMessageNode();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(-348, 698));
        node = m_node_builder->SpawnPrintStringNode();
        NodeEditor::SetNodePosition(node->GetID(), ImVec2(-69, 652));

        m_node_builder->BuildNodes();

        // Create initial links
        Node* node5 = m_node_builder->FindNode(5);
        Node* node6 = m_node_builder->FindNode(6);
        if (node5 && node6 && !node5->GetOutputs().empty() && !node6->GetInputs().empty())
        {
            m_node_builder->CreateLink(node5->GetOutputs()[0].GetID(), node6->GetInputs()[0].GetID());
        }

        Node* node14 = m_node_builder->FindNode(14);
        Node* node15 = m_node_builder->FindNode(15);
        if (node14 && node15 && !node14->GetOutputs().empty() && !node15->GetInputs().empty())
        {
            m_node_builder->CreateLink(node14->GetOutputs()[0].GetID(), node15->GetInputs()[0].GetID());
        }

        m_first_run = false;
    }
}

void NodeWidget::TouchNode(NodeEditor::NodeId id) { m_node_touch_time[id] = m_touch_time; }

float NodeWidget::GetTouchProgress(NodeEditor::NodeId id)
{
    if (auto it = m_node_touch_time.find(id); it != m_node_touch_time.end() && it->second > 0.0f) return (m_touch_time - it->second) / m_touch_time;
    return 0.0f;
}

void NodeWidget::UpdateTouch()
{
    const auto deltaTime = ImGui::GetIO().DeltaTime;
    for (auto& val : m_node_touch_time | views::values)
    {
        if (val > 0.0f) val -= deltaTime;
    }
}

ImColor NodeWidget::GetIconColor(PinType type)
{
    switch (type)
    {
        default:
        case PinType::Flow: return {255, 255, 255};
        case PinType::Bool: return {220, 48, 48};
        case PinType::Int: return {68, 201, 156};
        case PinType::Float: return {147, 226, 74};
        case PinType::String: return {124, 21, 153};
        case PinType::Object: return {51, 150, 215};
        case PinType::Function: return {218, 0, 183};
        case PinType::Delegate: return {255, 48, 48};
    }
}

void NodeWidget::DrawPinIcon(const Pin& pin, bool connected, int alpha)
{
    spartan::IconType iconType;
    ImColor color = GetIconColor(pin.GetType());
    color.Value.w = alpha / 255.0f;
    
    switch (pin.GetType())
    {
        case PinType::Flow: iconType = spartan::IconType::Flow; break;
        case PinType::Bool: iconType = spartan::IconType::Circle; break;
        case PinType::Int: iconType = spartan::IconType::Circle; break;
        case PinType::Float: iconType = spartan::IconType::Circle; break;
        case PinType::String: iconType = spartan::IconType::Circle; break;
        case PinType::Object: iconType = spartan::IconType::Circle; break;
        case PinType::Function: iconType = spartan::IconType::Circle; break;
        case PinType::Delegate: iconType = spartan::IconType::Square; break;
        default: return;
    }

    const float icon_size = static_cast<float>(m_pin_icon_size);
    ImVec4 tint = ImVec4(color.Value.x, color.Value.y, color.Value.z, color.Value.w);
    
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - icon_size * 0.25f);
    ImGuiSp::image(iconType, icon_size, tint);
}

void NodeWidget::DrawNode(Node* node)
{
    if (!node) return;

    const float rounding = 4.0f;
    const float padding = 12.0f;

    NodeEditor::PushStyleColor(NodeEditor::StyleColor_NodeBg, ImColor(75, 75, 75, 255));
    NodeEditor::PushStyleColor(NodeEditor::StyleColor_NodeBorder, ImColor(32, 32, 32, 255));
    NodeEditor::PushStyleColor(NodeEditor::StyleColor_PinRect, ImColor(60, 60, 60, 255));
    NodeEditor::PushStyleColor(NodeEditor::StyleColor_PinRectBorder, ImColor(60, 60, 60, 255));

    NodeEditor::PushStyleVar(NodeEditor::StyleVar_NodePadding, ImVec4(8, 8, 8, 8));
    NodeEditor::PushStyleVar(NodeEditor::StyleVar_NodeRounding, rounding);
    NodeEditor::PushStyleVar(NodeEditor::StyleVar_SourceDirection, ImVec2(0.0f, 1.0f));
    NodeEditor::PushStyleVar(NodeEditor::StyleVar_TargetDirection, ImVec2(0.0f, -1.0f));
    NodeEditor::PushStyleVar(NodeEditor::StyleVar_LinkStrength, 0.0f);
    NodeEditor::PushStyleVar(NodeEditor::StyleVar_PinBorderWidth, 1.0f);
    NodeEditor::PushStyleVar(NodeEditor::StyleVar_PinRadius, 5.0f);

    NodeEditor::BeginNode(node->GetID());

    ImGui::BeginVertical("node");
    ImGui::BeginHorizontal("node_header");
    ImGui::Spring(0);
    ImGui::TextUnformatted(node->GetName().c_str());
    ImGui::Spring(1);
    ImGui::Dummy(ImVec2(0, 28));
    ImGui::Spring(0);
    ImGui::EndHorizontal();

    ImGui::BeginHorizontal("content_frame");
    ImGui::Spring(1, 0);

    // Input pins
    ImGui::BeginVertical("inputs", ImVec2(0, 0), 0.0f);
    NodeEditor::PushStyleVar(NodeEditor::StyleVar_PivotAlignment, ImVec2(0, 0.5f));
    NodeEditor::PushStyleVar(NodeEditor::StyleVar_PivotSize, ImVec2(0, 0));

    for (auto& input : node->GetInputs())
    {
        ImGui::BeginHorizontal(input.GetID().Get());
        NodeEditor::BeginPin(input.GetID(), NodeEditor::PinKind::Input);
        NodeEditor::PinPivotAlignment(ImVec2(0.0f, 0.5f));
        NodeEditor::PinPivotSize(ImVec2(0, 0));

        DrawPinIcon(input, m_node_builder->IsPinLinked(input.GetID()), 255);

        NodeEditor::EndPin();
        
        ImGui::Spring(0);
        if (!input.GetName().empty())
        {
            ImGui::TextUnformatted(input.GetName().c_str());
            ImGui::Spring(0);
        }
        ImGui::EndHorizontal();
    }

    NodeEditor::PopStyleVar(2);
    ImGui::Spring(1, 0);
    ImGui::EndVertical();

    ImGui::Spring(1);

    // Output pins
    ImGui::BeginVertical("outputs", ImVec2(0, 0), 1.0f);
    NodeEditor::PushStyleVar(NodeEditor::StyleVar_PivotAlignment, ImVec2(1.0f, 0.5f));
    NodeEditor::PushStyleVar(NodeEditor::StyleVar_PivotSize, ImVec2(0, 0));

    for (auto& output : node->GetOutputs())
    {
        ImGui::BeginHorizontal(output.GetID().Get());
        ImGui::Spring(0);
        
        if (!output.GetName().empty())
        {
            ImGui::TextUnformatted(output.GetName().c_str());
            ImGui::Spring(0);
        }

        NodeEditor::BeginPin(output.GetID(), NodeEditor::PinKind::Output);
        NodeEditor::PinPivotAlignment(ImVec2(1.0f, 0.5f));
        NodeEditor::PinPivotSize(ImVec2(0, 0));

        DrawPinIcon(output, m_node_builder->IsPinLinked(output.GetID()), 255);

        NodeEditor::EndPin();
        ImGui::EndHorizontal();
    }

    NodeEditor::PopStyleVar(2);
    ImGui::Spring(1, 0);
    ImGui::EndVertical();

    ImGui::Spring(1, 0);
    ImGui::EndHorizontal();
    
    ImGui::EndVertical();

    NodeEditor::EndNode();
    NodeEditor::PopStyleVar(7);
    NodeEditor::PopStyleColor(4);

    if (ImGui::IsItemVisible())
    {
        auto alpha = static_cast<int>(255 * ImGui::GetStyle().Alpha);
        auto drawList = NodeEditor::GetNodeBackgroundDrawList(node->GetID());

        const auto& style = ImGui::GetStyle();
        const auto cursorPos = ImGui::GetCursorScreenPos();
    }
}

void NodeWidget::DrawNodes()
{
    for (int node_id = 1; node_id <= 15; ++node_id)
    {
        if (Node* node = m_node_builder->FindNode(node_id))
        {
            DrawNode(node);
        }
    }
}

void NodeWidget::HandleInteractions()
{
    // Handle link creation
    if (NodeEditor::BeginCreate(ImColor(255, 255, 255), 2.0f))
    {
        auto showLabel = [](const char* label, ImColor color)
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
            auto size = ImGui::CalcTextSize(label);

            auto padding = ImGui::GetStyle().FramePadding;
            auto spacing = ImGui::GetStyle().ItemSpacing;

            ImVec2 cursorPos = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(cursorPos.x + spacing.x, cursorPos.y - spacing.y));

            ImVec2 screenPos = ImGui::GetCursorScreenPos();
            ImVec2 rectMin = ImVec2(screenPos.x - padding.x, screenPos.y - padding.y);
            ImVec2 rectMax = ImVec2(screenPos.x + size.x + padding.x, screenPos.y + size.y + padding.y);

            auto drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
            ImGui::TextUnformatted(label);
        };

        NodeEditor::PinId startPinId = 0, endPinId = 0;
        if (NodeEditor::QueryNewLink(&startPinId, &endPinId))
        {
            auto startPin = m_node_builder->FindPin(startPinId);
            auto endPin = m_node_builder->FindPin(endPinId);

            m_new_link_pin = startPin ? startPin : endPin;

            if (startPin && startPin->GetKind() == PinKind::Input)
            {
                std::swap(startPin, endPin);
                std::swap(startPinId, endPinId);
            }

            if (startPin && endPin)
            {
                if (endPin == startPin)
                {
                    NodeEditor::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                }
                else if (endPin->GetKind() == startPin->GetKind())
                {
                    showLabel("x Incompatible Pin Kind", ImColor(45, 32, 32, 180));
                    NodeEditor::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                }
                else if (endPin->GetType() != startPin->GetType())
                {
                    showLabel("x Incompatible Pin Type", ImColor(45, 32, 32, 180));
                    NodeEditor::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                }
                else
                {
                    showLabel("+ Create Link", ImColor(32, 45, 32, 180));
                    if (NodeEditor::AcceptNewItem(ImColor(128, 255, 128), 4.0f))
                    {
                        m_node_builder->CreateLink(startPinId, endPinId);
                    }
                }
            }
        }

        NodeEditor::PinId pinId = 0;
        if (NodeEditor::QueryNewNode(&pinId))
        {
            m_new_link_pin = m_node_builder->FindPin(pinId);
            if (m_new_link_pin)
                showLabel("+ Create Node", ImColor(32, 45, 32, 180));

            if (NodeEditor::AcceptNewItem())
            {
                m_create_new_node = true;
                m_new_node_link_pin = m_node_builder->FindPin(pinId);
                m_new_link_pin = nullptr;
                NodeEditor::Suspend();
                ImGui::OpenPopup("Create New Node");
                NodeEditor::Resume();
            }
        }

        NodeEditor::EndCreate();
    }
    else
    {
        m_new_link_pin = nullptr;
    }

    // Handle link deletion
    if (NodeEditor::BeginDelete())
    {
        NodeEditor::LinkId linkId = 0;
        while (NodeEditor::QueryDeletedLink(&linkId))
        {
            if (NodeEditor::AcceptDeletedItem())
            {
                m_node_builder->DeleteLink(linkId);
            }
        }

        NodeEditor::NodeId nodeId = 0;
        while (NodeEditor::QueryDeletedNode(&nodeId))
        {
            if (NodeEditor::AcceptDeletedItem())
            {
                // Delete node logic would go here
            }
        }

        NodeEditor::EndDelete();
    }
}

void NodeWidget::ShowContextMenu()
{
    NodeEditor::Suspend();
    
    if (NodeEditor::ShowNodeContextMenu(&m_context_node_id))
        ImGui::OpenPopup("Node Context Menu");
    else if (NodeEditor::ShowPinContextMenu(&m_context_pin_id))
        ImGui::OpenPopup("Pin Context Menu");
    else if (NodeEditor::ShowLinkContextMenu(&m_context_link_id))
        ImGui::OpenPopup("Link Context Menu");
    else if (NodeEditor::ShowBackgroundContextMenu())
    {
        ImGui::OpenPopup("Create New Node");
        m_new_node_link_pin = nullptr;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    if (ImGui::BeginPopup("Node Context Menu"))
    {
        auto node = m_node_builder->FindNode(m_context_node_id);
        
        ImGui::TextUnformatted("Node Context Menu");
        ImGui::Separator();
        if (node)
            ImGui::Text("ID: %d", static_cast<int>(node->GetID().Get()));

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Pin Context Menu"))
    {
        auto pin = m_node_builder->FindPin(m_context_pin_id);

        ImGui::TextUnformatted("Pin Context Menu");
        ImGui::Separator();
        if (pin)
            ImGui::Text("ID: %d", static_cast<int>(pin->GetID().Get()));

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Link Context Menu"))
    {
        auto link = m_node_builder->FindLink(m_context_link_id);

        ImGui::TextUnformatted("Link Context Menu");
        ImGui::Separator();
        if (link)
            ImGui::Text("ID: %d", static_cast<int>(link->GetID().Get()));

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Create New Node"))
    {
        auto newNodePosition = NodeEditor::ScreenToCanvas(ImGui::GetMousePosOnOpeningCurrentPopup());

        Node* node = nullptr;
        if (ImGui::MenuItem("Input Action"))
            node = m_node_builder->SpawnInputActionNode();
        if (ImGui::MenuItem("Branch"))
            node = m_node_builder->SpawnBranchNode();
        if (ImGui::MenuItem("Do N"))
            node = m_node_builder->SpawnDoNNode();
        if (ImGui::MenuItem("Output Action"))
            node = m_node_builder->SpawnOutputActionNode();
        if (ImGui::MenuItem("Print String"))
            node = m_node_builder->SpawnPrintStringNode();
        if (ImGui::MenuItem("Set Timer"))
            node = m_node_builder->SpawnSetTimerNode();
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Comment"))
            node = m_node_builder->SpawnComment();

        if (node)
        {
            m_node_builder->BuildNode(node);
            NodeEditor::SetNodePosition(node->GetID(), newNodePosition);

            if (auto startPin = m_new_node_link_pin)
            {
                auto& pins = startPin->GetKind() == PinKind::Input ? node->GetOutputs() : node->GetInputs();

                for (auto& pin : pins)
                {
                    if (Pin::CanCreateLink(startPin, &pin))
                    {
                        auto endPin = &pin;
                        if (startPin->GetKind() == PinKind::Input)
                            std::swap(startPin, endPin);

                        m_node_builder->CreateLink(startPin->GetID(), endPin->GetID());

                        break;
                    }
                }
            }
        }

        ImGui::EndPopup();
    }
    else
        m_create_new_node = false;

    ImGui::PopStyleVar();
    NodeEditor::Resume();
}

void NodeWidget::OnTickVisible()
{
    // Guard: Only proceed if we have a valid context
    if (!m_context)
        return;

    ImVec2 content_region = ImGui::GetContentRegionAvail();
    ImVec2 size           = ImVec2(content_region.x * source_pane_vertical_split_percentage, content_region.y - source_pane_bottom_margin * spartan::Window::GetDpiScale());

    NodeEditor::SetCurrentEditor(m_context);
    
    // Begin the node editor frame
    NodeEditor::Begin(m_title, size);

    // Navigate to content on first frame after initialization
    static bool first_frame_after_init = true;
    if (first_frame_after_init && !m_first_run)
    {
        NodeEditor::NavigateToContent();
        first_frame_after_init = false;
    }

    // Draw all nodes
    DrawNodes();

    // Draw all links
    for (auto& link : m_node_builder->GetLinks())
    {
        NodeEditor::Link(link.GetID(), link.GetStartPinID(), link.GetEndPinID());
    }

    // Handle interactions
    HandleInteractions();

    // Show context menus
    ShowContextMenu();

    // Always call End() to match Begin()
    NodeEditor::End();
    NodeEditor::SetCurrentEditor(nullptr);
}
