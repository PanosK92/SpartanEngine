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

#pragma once

//= INCLUDES ===========================
#include "../Source/ImGuizmo/ImGuizmo.h"
#include "../Source/imgui.h"
#include "World/Components/Transform.h"
#include "World/Entity.h"
#include "Context.h"
#include "Rendering/Renderer.h"
#include "Input/Input.h"
//======================================

namespace ImGui::TransformGizmo
{
    static void apply_style()
    {
        ImGuizmo::Style& style           = ImGuizmo::GetStyle();
        style.TranslationLineThickness   = 6.0f;
        style.TranslationLineArrowSize   = 10.0f;
        style.RotationLineThickness      = 4.0f;
        style.RotationOuterLineThickness = 5.0f;
        style.ScaleLineThickness         = 6.0f;
        style.ScaleLineCircleSize        = 6.0f;
        style.HatchedAxisLineThickness   = 6.0f;
        style.CenterCircleSize           = 6.0f;
    }

    static void tick(Spartan::Context* context)
    {
        std::shared_ptr<Spartan::Camera> camera = context->GetSystem<Spartan::Renderer>()->GetCamera();
        if (!camera)
            return;

        // Get selected entity
        std::shared_ptr<Spartan::Entity> entity = camera->GetSelectedEntity();

        // Enable/disable gizmo
        ImGuizmo::Enable(entity != nullptr);
        if (!entity)
            return;

        // Switch between position, rotation and scale operations, with W, E and R respectively
        static ImGuizmo::OPERATION transform_operation = ImGuizmo::TRANSLATE;
        if (!camera->IsControledInFirstPerson())
        {
            if (context->GetSystem<Spartan::Input>()->GetKeyDown(Spartan::KeyCode::W))
            {
                transform_operation = ImGuizmo::TRANSLATE;
            }
            else if (context->GetSystem<Spartan::Input>()->GetKeyDown(Spartan::KeyCode::E))
            {
                transform_operation = ImGuizmo::ROTATE;
            }
            else if (context->GetSystem<Spartan::Input>()->GetKeyDown(Spartan::KeyCode::R))
            {
                transform_operation = ImGuizmo::SCALE;
            }
        }

        ImGuizmo::MODE transform_space = ImGuizmo::WORLD;

        // Get some data
        const Spartan::Math::Matrix& matrix_projection = camera->GetProjectionMatrix().Transposed();
        const Spartan::Math::Matrix& matrix_view       = camera->GetViewMatrix().Transposed();
        Spartan::Transform* transform                  = entity->GetComponent<Spartan::Transform>();

        // Begin
        const bool is_orthographic = false;
        ImGuizmo::SetOrthographic(is_orthographic);
        ImGuizmo::BeginFrame();

        // Map transform to ImGuizmo
        float matrix_delta[16];
        float translation[3] = { transform->GetPosition().x, transform->GetPosition().y, transform->GetPosition().z };
        float rotation[3]    = { transform->GetRotation().ToEulerAngles().x, transform->GetRotation().ToEulerAngles().y, transform->GetRotation().ToEulerAngles().z };
        float scale[3]       = { transform->GetScale().x, transform->GetScale().y, transform->GetScale().z };
        ImGuizmo::RecomposeMatrixFromComponents(&translation[0], rotation, scale, matrix_delta);

        // Set viewport rectangle
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(), ImGui::GetWindowHeight());
        ImGuizmo::Manipulate(&matrix_view.m00, &matrix_projection.m00, transform_operation, transform_space, matrix_delta, 0, 0);

        // Map ImGuizmo to transform
        if (ImGuizmo::IsUsing())
        {
            ImGuizmo::DecomposeMatrixToComponents(matrix_delta, translation, rotation, scale);

            transform->SetPosition(Spartan::Math::Vector3(translation[0], translation[1], translation[2]));
            transform->SetRotation(Spartan::Math::Quaternion::FromEulerAngles(rotation[0], rotation[1], rotation[2]));
            transform->SetScale(Spartan::Math::Vector3(scale[0], scale[1], scale[2]));
        }
    }

    static bool allow_picking()
    {
        return !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
    }
}
