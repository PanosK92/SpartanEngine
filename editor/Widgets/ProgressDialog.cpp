/*
Copyright(c) 2016-2024 Panos Karabelas

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

namespace
{
    void show_progress_bar(const float fraction, const char* text, const bool top_seperator)
    {
        ImGui::Text("Hold on...");

        if (top_seperator)
        {
            ImGui::Separator();
        }

        ImGui::BeginGroup();
        {
            ImGui::ProgressBar(fraction, ImVec2(0.0f, 0.0f));
            ImGui::Text(text);
        }
        ImGui::EndGroup();
    }
}

ProgressDialog::ProgressDialog(Editor* editor) : Widget(editor)
{
    m_visible       = false;
    m_size_initial  = Vector2(500.0f, 83.0f);
    m_flags        |=
        ImGuiWindowFlags_NoMove           |
        ImGuiWindowFlags_NoCollapse       |
        ImGuiWindowFlags_NoScrollbar      |
        ImGuiWindowFlags_NoDocking        |
        ImGuiWindowFlags_NoTitleBar       |
        ImGuiWindowFlags_AlwaysAutoResize;
}

void ProgressDialog::OnTick()
{
    SetVisible(ProgressTracker::IsLoading());
}

void ProgressDialog::OnTickVisible()
{
    ImGui::SetWindowFocus();

    bool at_least_one_progress = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(ProgressType::Max); i++)
    {
        if (Progress* progress = &ProgressTracker::GetProgress(static_cast<ProgressType>(i)))
        {
            if (progress->IsProgressing())
            {
                show_progress_bar(progress->GetFraction(), progress->GetText().c_str(), at_least_one_progress);
                at_least_one_progress = true;
            }
        }
    }

    if (!at_least_one_progress)
    {
        show_progress_bar(0.0f, "...", at_least_one_progress);
    }
}
