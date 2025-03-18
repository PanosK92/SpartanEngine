/*
Copyright(c) 2016-2025 Panos Karabelas

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

#pragma once

//= INCLUDES =========================
#include "Source/ImGuizmo/ImGuizmo.h"
#include "Source/imgui.h"
#include "World/Entity.h"
#include "World/Components/Camera.h"
#include "Rendering/Renderer.h"
#include "Input/Input.h"
#include "Commands/CommandStack.h"
#include "Commands/CommandTransform.h"
#include "Engine.h"
//====================================

namespace ImGui::TransformGizmo
{
    const  spartan::math::Vector3 snap = spartan::math::Vector3(0.1f, 0.1f, 0.1f);

    bool first_use = true;
    spartan::math::Vector3 position_previous;
    spartan::math::Quaternion rotation_previous;
    spartan::math::Vector3 scale_previous;

    void apply_style()
    {
        const ImVec4 inspector_color_x = ImVec4(0.75f, 0.20f, 0.20f, 0.80f);
        const ImVec4 inspector_color_y = ImVec4(0.20f, 0.75f, 0.20f, 0.80f);
        const ImVec4 inspector_color_z = ImVec4(0.20f, 0.20f, 0.75f, 0.80f);

        ImGuizmo::Style& style                            = ImGuizmo::GetStyle();
        style.Colors[ImGuizmo::COLOR::DIRECTION_X]        = ImVec4(inspector_color_x.x, inspector_color_x.y, inspector_color_x.z, 1.0f);
        style.Colors[ImGuizmo::COLOR::DIRECTION_Y]        = ImVec4(inspector_color_y.x, inspector_color_y.y, inspector_color_y.z, 1.0f);
        style.Colors[ImGuizmo::COLOR::DIRECTION_Z]        = ImVec4(inspector_color_z.x, inspector_color_z.y, inspector_color_z.z, 1.0f);
        style.Colors[ImGuizmo::COLOR::PLANE_X]            = inspector_color_x;
        style.Colors[ImGuizmo::COLOR::PLANE_Y]            = inspector_color_y;
        style.Colors[ImGuizmo::COLOR::PLANE_Z]            = inspector_color_z;
        style.Colors[ImGuizmo::COLOR::HATCHED_AXIS_LINES] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        style.CenterCircleSize           = 5.0f;
        style.TranslationLineThickness   = 4.0f;
        style.TranslationLineArrowSize   = 6.0f;
        style.RotationLineThickness      = 3.0f;
        style.RotationOuterLineThickness = 2.0f;
        style.ScaleLineThickness         = 4.0f;
        style.ScaleLineCircleSize        = 7.0f;
    }

    static void tick()
    {
        if (spartan::Engine::IsFlagSet(spartan::EngineMode::Playing))
            return;

        spartan::Camera* camera = spartan::Renderer::GetCamera();
        if (!camera)
            return;

        // get selected entity
        std::shared_ptr<spartan::Entity> entity = camera->GetSelectedEntity();

        // enable/disable gizmo
        ImGuizmo::Enable(entity != nullptr);
        if (!entity)
            return;

        // switch between position, rotation and scale operations, with W, E and R respectively
        static ImGuizmo::OPERATION transform_operation = ImGuizmo::TRANSLATE;
        if (!camera->GetFlag(spartan::CameraFlags::IsActivelyControlled))
        {
            if (spartan::Input::GetKeyDown(spartan::KeyCode::W))
            {
                transform_operation = ImGuizmo::TRANSLATE;
            }
            else if (spartan::Input::GetKeyDown(spartan::KeyCode::E))
            {
                transform_operation = ImGuizmo::ROTATE;
            }
            else if (spartan::Input::GetKeyDown(spartan::KeyCode::R))
            {
                transform_operation = ImGuizmo::SCALE;
            }
        }

        // get matrices
        const spartan::math::Matrix& matrix_view       = camera->GetViewMatrix().Transposed();
        const spartan::math::Matrix& matrix_projection = camera->GetProjectionMatrix().Transposed();
 
        // begin
        const bool is_orthographic = false;
        ImGuizmo::SetOrthographic(is_orthographic);
        ImGuizmo::BeginFrame();

        // map transform to ImGuizmo
        spartan::math::Vector3 position        = entity->GetPosition();
        spartan::math::Vector3 scale           = entity->GetScale();
        spartan::math::Quaternion rotation     = entity->GetRotation();
        spartan::math::Matrix transform_matrix = spartan::math::Matrix::GenerateRowFirst(position, rotation, scale);

        // set viewport rectangle
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(), ImGui::GetWindowHeight());

        ImGuizmo::Manipulate(
            &matrix_view.m00,
            &matrix_projection.m00,
            transform_operation,
            ImGuizmo::WORLD,
            &transform_matrix.m00,
            nullptr,
            &snap.x
        );

        // map imguizmo to transform
        if (ImGuizmo::IsUsing())
        {
            // start of handling - save the initial transform
            if (first_use)
            {
                position_previous = entity->GetPosition();
                rotation_previous = entity->GetRotation();
                scale_previous    = entity->GetScale();

                first_use = false;
            }

            transform_matrix.Transposed().Decompose(scale, rotation, position);
            entity->SetPosition(position);
            entity->SetRotation(rotation);
            entity->SetScale(scale);

            // end of handling - add the current and previous transforms to the command stack
            if (spartan::Input::GetKeyUp(spartan::KeyCode::Click_Left))
            {
                spartan::CommandStack::Add<spartan::CommandTransform>(entity.get(), position_previous, rotation_previous, scale_previous);
                first_use = true;
            }
        }
    }

    static bool allow_picking()
    {
        return !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
    }
}
