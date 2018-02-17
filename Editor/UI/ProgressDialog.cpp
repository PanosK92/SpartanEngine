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

//= INCLUDES ==============
#include "ProgressDialog.h"
#include "ImGui/imgui.h"
#include "EditorHelper.h"
//=========================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

static float width = 500.0f;

ProgressDialog::ProgressDialog(char* title /*Hold on...*/)
{
	m_title		= title;
	m_isVisible	= true;
	m_progress	= 0.0f;
}

ProgressDialog::~ProgressDialog()
{

}

void ProgressDialog::Update()
{
	if (!m_isVisible)
		return;

	ShowProgressBar();
}

void ProgressDialog::ShowProgressBar()
{
	// Window begin
	ImGui::SetNextWindowSize(ImVec2(width, 73), ImGuiCond_Always);
	ImGui::Begin(m_title.c_str(), &m_isVisible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
	ImGui::SetWindowFocus();

	// Progress	
	ImGui::PushItemWidth(width - ImGui::GetStyle().WindowPadding.x * 2.0f);
	ImGui::ProgressBar(m_progress, ImVec2(0.0f, 0.0f));
	ImGui::Text(m_progressStatus.c_str());
	ImGui::PopItemWidth();

	// Window end
	ImGui::End();
}
