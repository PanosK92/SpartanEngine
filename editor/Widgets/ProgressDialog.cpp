/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include <array>
//===============================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan;
using namespace Spartan::Math;
//============================

namespace
{
    static array<Progress*, 5> progresses;
}

ProgressDialog::ProgressDialog(Editor* editor) : Widget(editor)
{
    m_title         = "Hold on...";
    m_visible       = false;
    m_size_initial  = Vector2(500.0f, 83.0f);
    m_flags        |= ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
}

void ProgressDialog::OnTick()
{
    progresses[static_cast<uint32_t>(ProgressType::ModelImporter)] = &ProgressTracker::GetProgress(ProgressType::ModelImporter);
    progresses[static_cast<uint32_t>(ProgressType::World)]         = &ProgressTracker::GetProgress(ProgressType::World);
    progresses[static_cast<uint32_t>(ProgressType::Resource)]      = &ProgressTracker::GetProgress(ProgressType::Resource);
    progresses[static_cast<uint32_t>(ProgressType::Terrain)]       = &ProgressTracker::GetProgress(ProgressType::Terrain);

    // show only if an operation is in progress
    bool visible = ProgressTracker::IsLoading();
    SetVisible(visible);
}

void ProgressDialog::OnTickVisible()
{
    ImGui::SetWindowFocus();

    bool first = true;
    for (Progress* progress : progresses)
    {
        if (progress && progress->IsProgressing())
        {
            if (!first)
            {
                ImGui::Separator();
            }

            ImGui::BeginGroup();
            ImGui::ProgressBar(progress->GetFraction(), ImVec2(0.0f, 0.0f));
            ImGui::Text(progress->GetText().c_str());
            ImGui::EndGroup();
            first = false;
        }
    }
}
