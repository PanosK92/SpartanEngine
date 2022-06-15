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
    ShowBookmarks();
}

void CameraBookmarkViewer::ShowBookmarks()
{
    enum class Axis
    {
        x,
        y,
        z
    };

    const auto show_float = [](Axis axis, float* value)
    {
        const float label_float_spacing = 15.0f;
        const float step = 0.01f;
        const string format = "%.4f";

        // Label
        ImGui::TextUnformatted(axis == Axis::x ? "x" : axis == Axis::y ? "y" : "z");
        ImGui::SameLine(label_float_spacing);
        Vector2 pos_post_label = ImGui::GetCursorScreenPos();

        // Float
        ImGui::PushItemWidth(128.0f);
        ImGui::PushID(static_cast<int>(ImGui::GetCursorPosX() + ImGui::GetCursorPosY()));
        ImGuiEx::DragFloatWrap("##no_label", value, step, numeric_limits<float>::lowest(), numeric_limits<float>::max(), format.c_str());
        ImGui::PopID();
        ImGui::PopItemWidth();

        // Axis color
        static const ImU32 color_x = IM_COL32(168, 46, 2, 255);
        static const ImU32 color_y = IM_COL32(112, 162, 22, 255);
        static const ImU32 color_z = IM_COL32(51, 122, 210, 255);
        static const Vector2 size = Vector2(4.0f, 19.0f);
        static const Vector2 offset = Vector2(5.0f, 4.0);
        pos_post_label += offset;
        ImRect axis_color_rect = ImRect(pos_post_label.x, pos_post_label.y, pos_post_label.x + size.x, pos_post_label.y + size.y);
        ImGui::GetWindowDrawList()->AddRectFilled(axis_color_rect.Min, axis_color_rect.Max, axis == Axis::x ? color_x : axis == Axis::y ? color_y : color_z);
    };

    const auto show_vector = [&show_float](const char* label, Vector3& vector)
    {
        const float label_indetation = 15.0f;

        ImGui::BeginGroup();
        ImGui::Indent(label_indetation);
        ImGui::TextUnformatted(label);
        ImGui::Unindent(label_indetation);
        show_float(Axis::x, &vector.x);
        show_float(Axis::y, &vector.y);
        show_float(Axis::z, &vector.z);
        ImGui::EndGroup();
    };

    if (shared_ptr<Camera> camera = m_context->GetSubsystem<Renderer>()->GetCamera())
    {
        const vector<camera_bookmark>& camera_bookmarks = camera->GetBookmarks();
        for (int i = 0; i < camera_bookmarks.size(); ++i)
        {
            Vector3 position = camera_bookmarks[i].position;
            Vector3 rotation = camera_bookmarks[i].rotation;

            show_vector("Position", position);
            ImGui::SameLine();
            show_vector("Rotation", rotation);
            ImGui::SameLine();
            ShowGoToBookmarkButton(i);
        }
    }

    ShowAddBookmarkButton();
}

void CameraBookmarkViewer::ShowAddBookmarkButton()
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
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
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - 5);

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
