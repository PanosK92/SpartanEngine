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

//= INCLUDES ==========================
#include "Widget.h"
#include "../../ImGui/imgui.h"
#include "../../ImGui/imgui_internal.h"
//=====================================

//= NAMESPACES ==========
using namespace Directus;
//=======================

void Widget::Initialize(Context* context)
{
	m_windowFlags	= ImGuiWindowFlags_NoCollapse;
	m_context		= context;
	m_xMin			= 455;
	m_yMin			= 400;
	m_xMax			= FLT_MAX;
	m_yMax			= FLT_MAX;
}

void Widget::Begin()
{
	ImGui::SetNextWindowSize(ImVec2(m_xMin, m_yMin), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(m_xMin, m_yMin), ImVec2(m_xMax, m_yMax));
	ImGui::Begin(m_title.c_str(), &m_isVisible, m_windowFlags);
}

void Widget::End()
{
	m_window = ImGui::GetCurrentWindow();
	m_height = ImGui::GetWindowHeight();
	ImGui::End();
}
