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

//= INCLUDES =======================
#include "Widget_ProgressDialog.h"
#include "Resource/ProgressReport.h"
//==================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

namespace _Widget_ProgressDialog
{
	static float width = 500.0f;
}

Widget_ProgressDialog::Widget_ProgressDialog(Context* contex) : Widget(contex)
{
	m_title			= "Hold on...";
	m_isVisible		= false;
	m_progress		= 0.0f;
	m_xMin			= _Widget_ProgressDialog::width;
	m_yMin			= 83.0f;
	m_windowFlags	|= ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking;
}

bool Widget_ProgressDialog::Begin()
{
	// Determine if an operation is in progress
	ProgressReport& progressReport = ProgressReport::Get();
	bool isLoadingModel				= progressReport.GetIsLoading(g_progress_ModelImporter);
	bool isLoadingScene				= progressReport.GetIsLoading(g_progress_Scene);
	bool inProgress					= isLoadingModel || isLoadingScene;

	// Acquire progress
	if (isLoadingModel)
	{
		m_progress = progressReport.GetPercentage(Directus::g_progress_ModelImporter);
		m_progressStatus = progressReport.GetStatus(Directus::g_progress_ModelImporter);
	}
	else if (isLoadingScene)
	{
		m_progress = progressReport.GetPercentage(Directus::g_progress_Scene);
		m_progressStatus = progressReport.GetStatus(Directus::g_progress_Scene);
	}

	// Show only if an operation is in progress
	m_isVisible = inProgress;

	return Widget::Begin();
}

void Widget_ProgressDialog::Tick(float deltaTime)
{
	if (!m_isVisible)
		return;

	// Show dialog
	ImGui::SetWindowFocus();
	ImGui::PushItemWidth(_Widget_ProgressDialog::width - ImGui::GetStyle().WindowPadding.x * 2.0f);
	ImGui::ProgressBar(m_progress, ImVec2(0.0f, 0.0f));
	ImGui::Text(m_progressStatus.c_str());
	ImGui::PopItemWidth();
}