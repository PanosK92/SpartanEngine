/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ====================
#include "ProgressDialog.h"
#include "Core/ProgressTracker.h"
//===============================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan;
using namespace Spartan::Math;
//============================

ProgressDialog::ProgressDialog(Editor* editor) : Widget(editor)
{
    m_title        = "Hold on...";
    m_visible      = false;
    m_progress     = 0.0f;
    m_size_initial = Vector2(500.0f, 83.0f);
    m_flags        |= ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking;
    m_position     = k_widget_position_screen_center;
}

void ProgressDialog::TickAlways()
{
    // Determine if an operation is in progress
    const bool is_loading_model = ProgressTracker::GetProgress(ProgressType::model_importing).IsLoading();
    const bool is_loading_scene = ProgressTracker::GetProgress(ProgressType::world_io).IsLoading();
    const bool in_progress      = is_loading_model || is_loading_scene;

    // Acquire progress
    if (is_loading_model)
    {
        m_progress       = ProgressTracker::GetProgress(ProgressType::model_importing).GetFraction();
        m_progressStatus = ProgressTracker::GetProgress(ProgressType::model_importing).GetText();
    }
    else if (is_loading_scene)
    {
        m_progress       = ProgressTracker::GetProgress(ProgressType::world_io).GetFraction();
        m_progressStatus = ProgressTracker::GetProgress(ProgressType::world_io).GetText();
    }

    // Show only if an operation is in progress
    SetVisible(in_progress);
}

void ProgressDialog::TickVisible()
{
    ImGui::SetWindowFocus();
    ImGui::PushItemWidth(m_size_initial.x - ImGui::GetStyle().WindowPadding.x * 2.0f);
    ImGui::ProgressBar(m_progress, ImVec2(0.0f, 0.0f));
    ImGui::Text(m_progressStatus.c_str());
    ImGui::PopItemWidth();
}
