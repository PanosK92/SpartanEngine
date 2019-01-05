/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ===============
#include "Widget_Console.h"
#include "../IconProvider.h"
#include "../EditorHelper.h"
//==========================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
//=======================

namespace _Widget_Console
{
	static bool scrollToBottom = false;
	static const vector<Vector4> colors =
	{
		Vector4(0.76f, 0.77f, 0.8f, 1.0f),	// Info
		Vector4(0.75f, 0.75f, 0.0f, 1.0f),	// Warning
		Vector4(0.75f, 0.0f, 0.0f, 1.0f)	// Error
	};
	static ImGuiTextFilter logFilter;
}

Widget_Console::Widget_Console(Context* context) : Widget(context)
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

void Widget_Console::Tick(float deltaTime)
{
	// Clear Button
	if (ImGui::Button("Clear"))	{ Clear();} ImGui::SameLine();

	// Lambda for info, warning, error filter button
	auto DisplayButton = [](Icon_Type icon, bool* toggle)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, *toggle ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
		if (THUMBNAIL_BUTTON_BY_TYPE(icon, 15.0f))
		{
			*toggle = !(*toggle);
			_Widget_Console::scrollToBottom = true;
		}
		ImGui::PopStyleColor();
		ImGui::SameLine();
	};

	// Log category visibility buttons
	DisplayButton(Icon_Console_Info, &m_showInfo);
	DisplayButton(Icon_Console_Warning, &m_showWarnings);
	DisplayButton(Icon_Console_Error, &m_showErrors);

	// Text filter
	_Widget_Console::logFilter.Draw("Filter", -100.0f);
	ImGui::Separator();

	// Content
	ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
	for (auto& log : m_logs)
	{
		if (!_Widget_Console::logFilter.PassFilter(log.text.c_str()))
			continue;

		if ((log.errorLevel == 0 && m_showInfo) || (log.errorLevel == 1 && m_showWarnings) || (log.errorLevel == 2 && m_showErrors))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, _Widget_Console::colors[log.errorLevel]);	// text
			ImGui::TextUnformatted(log.text.c_str());
			ImGui::PopStyleColor();
		}
	}

	if (_Widget_Console::scrollToBottom)
	{
		ImGui::SetScrollHereY();
		_Widget_Console::scrollToBottom = false;
	}

	ImGui::EndChild();
}

void Widget_Console::AddLogPackage(LogPackage package)
{
	m_logs.push_back(package);
	if ((unsigned int)m_logs.size() > m_maxLogEntries)
	{
		m_logs.pop_front();
	}

	_Widget_Console::scrollToBottom = true;
}

void Widget_Console::Clear()
{
	m_logs.clear();
	m_logs.shrink_to_fit();
}
