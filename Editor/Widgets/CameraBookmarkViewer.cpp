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
    m_title        = "Camera Bookmark Viewer";
    m_size_initial = 500;
}

void CameraBookmarkViewer::TickVisible()
{
    ShowBookmarks();
}

void CameraBookmarkViewer::ShowBookmarks()
{
    const bool is_playing = m_context->m_engine->EngineMode_IsSet(Engine_Game);
    if (is_playing)
    {
        return;
    }

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
    const std::vector<CameraBookmark>& cameraBookmarks = m_context->GetSubsystem<World>()->GetCameraBookmarks();
    for (int i = 0; i < cameraBookmarks.size(); ++i)
    {
        Vector3 position = cameraBookmarks[i].position;
        Vector3 rotation = cameraBookmarks[i].rotation;

        show_vector("Position", position);
        ImGui::SameLine();
        show_vector("Rotation", rotation);
        ImGui::SameLine();
        ShowGoToBookmarkButton(i);
    }

    ShowAddBookmarkButton();
}

void CameraBookmarkViewer::ShowAddBookmarkButton()
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - 50);
    if (ImGuiEx::Button("Add Bookmark"))
    {
        if (auto camera = m_context->GetSubsystem<World>()->EntityGetByName("Camera"))
        {
            auto transform = camera->GetComponent<Transform>();
            AddCameraBookmark({transform->GetPosition(), transform->GetRotation().ToEulerAngles()});
        }
    }
}

void CameraBookmarkViewer::ShowGoToBookmarkButton(int bookmarkIndex)
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 50);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - 5);

    //Not the best allocation friendly. Find a way to refer buttons other than names.
    std::string buttonLabel = "Go To Bookmark " + to_string(bookmarkIndex);
    if (ImGuiEx::Button(buttonLabel.c_str()))
    {
        GoToBookmark(bookmarkIndex);
    }
}

void CameraBookmarkViewer::GoToBookmark(int bookmarkIndex)
{
    if (auto camera = m_context->GetSubsystem<World>()->EntityGetByName("Camera"))
    {
        const std::vector<CameraBookmark>& cameraBookmarks = m_context->GetSubsystem<World>()->GetCameraBookmarks();
        LOG_INFO("CameraBookmark: Position = %s, Rotation = %s", cameraBookmarks[bookmarkIndex].position.ToString().c_str(), cameraBookmarks[bookmarkIndex].rotation.ToString().c_str());
        auto cameraComponent = camera->GetComponent<Camera>();
        cameraComponent->GoToCameraBookmark(bookmarkIndex);
    }
}

void CameraBookmarkViewer::AddCameraBookmark(CameraBookmark bookmark)
{
    m_context->GetSubsystem<World>()->AddCameraBookmark(bookmark);
}
