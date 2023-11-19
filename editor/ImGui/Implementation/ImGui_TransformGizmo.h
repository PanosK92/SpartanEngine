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
#include "Rendering/Renderer.h"
#include "Input/Input.h"
#include "Commands/CommandStack.h"
#include "Commands/TransformEntity.h"
#include "Engine.h"
//======================================

namespace ImGui::TransformGizmo
{
    static inline bool previous_handled = false;

    static inline Spartan::Math::Vector3 begin_position;
    static inline Spartan::Math::Quaternion begin_rotation;
    static inline Spartan::Math::Vector3 begin_scale;

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

    static void tick()
    {
        if (Spartan::Engine::IsFlagSet(Spartan::EngineMode::Game))
            return;

        std::shared_ptr<Spartan::Camera> camera = Spartan::Renderer::GetCamera();
        if (!camera)
            return;

        // Get selected entity
        std::shared_ptr<Spartan::Entity> entity = camera->GetSelectedEntity();

        // Enable/disable gizmo
        ImGuizmo::Enable(entity != nullptr);
        if (!entity)
        {
            return;
        }

        // Switch between position, rotation and scale operations, with W, E and R respectively
        static ImGuizmo::OPERATION transform_operation = ImGuizmo::TRANSLATE;
        if (!camera->IsActivelyControlled())
        {
            if (Spartan::Input::GetKeyDown(Spartan::KeyCode::W))
            {
                transform_operation = ImGuizmo::TRANSLATE;
            }
            else if (Spartan::Input::GetKeyDown(Spartan::KeyCode::E))
            {
                transform_operation = ImGuizmo::ROTATE;
            }
            else if (Spartan::Input::GetKeyDown(Spartan::KeyCode::R))
            {
                transform_operation = ImGuizmo::SCALE;
            }
        }

        ImGuizmo::MODE transform_space = ImGuizmo::WORLD;

        // Get some data
        const Spartan::Math::Matrix& matrix_projection = camera->GetProjectionMatrix().Transposed();
        const Spartan::Math::Matrix& matrix_view       = camera->GetViewMatrix().Transposed();
        std::shared_ptr<Spartan::Transform> transform  = entity->GetComponent<Spartan::Transform>();

        // Begin
        const bool is_orthographic = false;
        ImGuizmo::SetOrthographic(is_orthographic);
        ImGuizmo::BeginFrame();

        // Map transform to ImGuizmo
        Spartan::Math::Vector3 position    = transform->GetPosition();
        Spartan::Math::Vector3 scale       = transform->GetScale();
        Spartan::Math::Quaternion rotation = transform->GetRotation();

        Spartan::Math::Matrix transform_matrix = Spartan::Math::Matrix::GenerateRowFirst(position, rotation, scale);

        // Set viewport rectangle
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(), ImGui::GetWindowHeight());

        bool this_handled = ImGuizmo::Manipulate(&matrix_view.m00, &matrix_projection.m00, transform_operation, transform_space, &transform_matrix.m00, nullptr, nullptr);
        bool began_handling = !previous_handled && this_handled;
        bool ended_handling = previous_handled && !this_handled;
        previous_handled = this_handled;

        // Map ImGuizmo to transform
        if (ImGuizmo::IsUsing())
        {
            transform_matrix.Transposed().Decompose(scale, rotation, position);

            if (began_handling)
            {
                begin_position = position;
                begin_rotation = rotation;
                begin_scale = scale;
            }

            transform->SetPosition(position);
            transform->SetRotation(rotation);
            transform->SetScale(scale);

            if (ended_handling)
            {
                SP_LOG_INFO("Applying command");
                Spartan::CommandStack::Apply<Spartan::TransformEntity>(entity.get(), begin_position, begin_rotation, begin_scale);
            }
        }
    }

    static bool allow_picking()
    {
        return !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
    }
}
