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

//= INCLUDES =====================================
#include "CameraBookmarkViewer.h"
#include "../ImGuiExtension.h"
#include "../ImGui/Source/imgui_stdlib.h"
#include "../ImGui/Source/imgui_internal.h"
#include "Core/Context.h"
#include "Core/Engine.h"
#include "World/Entity.h"
#include "World/Components/Transform.h"
//===============================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

CameraBookmarkViewer::CameraBookmarkViewer(Editor* editor) : Widget(editor)
{
    m_title        = "Camera bookmark viewer";
    m_size_initial = 500;
    m_visible      = false;
}

void CameraBookmarkViewer::TickVisible()
{
    if (shared_ptr<Camera> camera = m_context->GetSubsystem<Renderer>()->GetCamera())
    {
        const vector<camera_bookmark>& camera_bookmarks = camera->GetBookmarks();
        for (int i = 0; i < camera_bookmarks.size(); ++i)
        {
            Vector3 position = camera_bookmarks[i].position;
            Vector3 rotation = camera_bookmarks[i].rotation;

            ImGuiEx::DisplayVector3("Position", position);
            ImGui::SameLine();
            ImGuiEx::DisplayVector3("Rotation", rotation);
            ImGui::SameLine();

            ShowGoToBookmarkButton(i);
        }
    }

    ShowAddBookmarkButton();
}

void CameraBookmarkViewer::ShowAddBookmarkButton()
{
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - 50);

    if (ImGuiEx::Button("Add Bookmark"))
    {
        if (shared_ptr<Camera> camera = m_context->GetSubsystem<Renderer>()->GetCamera())
        {
            Transform* transform = camera->GetTransform();
            AddCameraBookmark({transform->GetPosition(), transform->GetRotation().ToEulerAngles()});
        }
    }
}

void CameraBookmarkViewer::ShowGoToBookmarkButton(const int bookmark_index)
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 50);
    ImGui::SameLine();

    //Not the best allocation friendly. Find a way to refer buttons other than names.
    string buttonLabel = "Go To Bookmark " + to_string(bookmark_index);
    if (ImGuiEx::Button(buttonLabel.c_str()))
    {
        GoToBookmark(bookmark_index);
    }
}

void CameraBookmarkViewer::GoToBookmark(const int bookmark_index)
{
    if (shared_ptr<Camera> camera = m_context->GetSubsystem<Renderer>()->GetCamera())
    {
        const vector<camera_bookmark>& camera_bookmarks = camera->GetBookmarks();
        LOG_INFO("CameraBookmark: Position = %s, Rotation = %s", camera_bookmarks[bookmark_index].position.ToString().c_str(), camera_bookmarks[bookmark_index].rotation.ToString().c_str());
        camera->GoToCameraBookmark(bookmark_index);
    }
}

void CameraBookmarkViewer::AddCameraBookmark(camera_bookmark bookmark)
{
    if (shared_ptr<Camera> camera = m_context->GetSubsystem<Renderer>()->GetCamera())
    {
        camera->AddBookmark(bookmark);
    }
}
