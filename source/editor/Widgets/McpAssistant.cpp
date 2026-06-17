/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES =======================
#include "pch.h"
#include "McpAssistant.h"
#include "MCP/McpServer.h"
//==================================

McpAssistant::McpAssistant(Editor* editor) : Widget(editor)
{
    m_title        = "MCP Assistant";
    m_visible      = false;
    m_size_initial = spartan::math::Vector2(520.0f, 340.0f);
    m_size_min     = spartan::math::Vector2(360.0f, 260.0f);
    m_status       = "Click MCP, type a prompt, then press Send.";
}

void McpAssistant::OnTickVisible()
{
    const bool is_running = spartan::McpServer::IsRunning();

    ImGui::Text("Engine control");
    ImGui::SameLine();
    ImGui::TextColored(is_running ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f) : ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "%s", is_running ? "active" : "inactive");

    if (is_running)
    {
        ImGui::TextDisabled("127.0.0.1:%u", spartan::McpServer::GetPort());
        if (ImGui::Button("Stop MCP"))
        {
            spartan::McpServer::Shutdown();
            m_status = "MCP stopped.";
        }
    }
    else if (ImGui::Button("Start MCP"))
    {
        spartan::McpServer::Start();
    }

    ImGui::Separator();

    ImGui::TextUnformatted("Prompt");
    ImGui::InputTextMultiline(
        "##mcp_prompt",
        m_prompt.data(),
        m_prompt.size(),
        ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 8.0f)
    );

    const bool has_prompt = m_prompt[0] != '\0';
    if (!has_prompt)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Send"))
    {
        SubmitPrompt();
    }

    if (!has_prompt)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        m_prompt[0] = '\0';
        m_status = "Prompt cleared.";
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", m_status.c_str());
}

void McpAssistant::SubmitPrompt()
{
    if (!spartan::McpServer::IsRunning())
    {
        spartan::McpServer::Start();
    }

    m_status =
        "Prompt captured, but Cursor prompt execution is not wired yet. "
        "The engine endpoint is active; the next step is connecting this box to a local Cursor SDK helper.";
}
