/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "Widget_Console.h"
#include "../../ImGui/Source/imgui.h"
#include "Logging/Log.h"
#include "../ThumbnailProvider.h"
#include "../EditorHelper.h"
//==================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

static bool g_scrollToBottom = false;
static ImVec4 g_logColor[3] =
{
	ImVec4(0.76f, 0.77f, 0.8f, 1.0f),	// Info
	ImVec4(1.0f, 1.0f, 0.0f, 1.0f),		// Warning
	ImVec4(1.0f, 0.0f, 0.0f, 1.0f)		// Error
};
static ImGuiTextFilter g_logFilter;

Widget_Console::Widget_Console()
{
	m_title = "Console";

	// Create an implementation of EngineLogger
	m_logger = make_shared<EngineLogger>();
	m_logger->SetCallback([this](LogPackage package) { AddLogPackage(package); });

	// Set the logger implementation for the engine to use
	Log::SetLogger(m_logger);

	m_showInfo		= true;
	m_showWarnings	= true;
	m_showErrors	= true;
}

void Widget_Console::Update()
{
	if (ImGui::Button("Clear"))									{ Clear(); }														ImGui::SameLine();
	if (THUMBNAIL_BUTTON_BY_TYPE(Icon_Console_Info,		15.0f))	{ m_showInfo		= !m_showInfo;		g_scrollToBottom = true; }	ImGui::SameLine();
	if (THUMBNAIL_BUTTON_BY_TYPE(Icon_Console_Warning,	15.0f))	{ m_showWarnings	= !m_showWarnings;	g_scrollToBottom = true;}	ImGui::SameLine();
	if (THUMBNAIL_BUTTON_BY_TYPE(Icon_Console_Error,	15.0f))	{ m_showErrors		= !m_showErrors;	g_scrollToBottom = true;}	ImGui::SameLine();

	g_logFilter.Draw("Filter", -100.0f);
	ImGui::Separator();

	ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
	for (const auto& log : m_logs)
	{
		if (!g_logFilter.PassFilter(log.text.c_str()))
			continue;

		if ((log.errorLevel == 0 && m_showInfo) || (log.errorLevel == 1 && m_showWarnings) || (log.errorLevel == 2 && m_showErrors))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, g_logColor[log.errorLevel]);
			ImGui::TextUnformatted(log.text.c_str());
			ImGui::PopStyleColor();
		}
	}

	if (g_scrollToBottom)
	{
		ImGui::SetScrollHere();
		g_scrollToBottom = false;
	}

	ImGui::EndChild();
}

void Widget_Console::AddLogPackage(LogPackage package)
{
	m_logs.push_back(package);
	if ((int)m_logs.size() > m_maxLogEntries)
	{
		m_logs.pop_front();
	}

	g_scrollToBottom = true;
}

void Widget_Console::Clear()
{
	m_logs.clear();
	m_logs.shrink_to_fit();
}
