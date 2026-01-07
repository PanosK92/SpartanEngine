/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ==============================
#include "Source/ImGuizmo/ImGuizmo.h"
#include "Source/imgui.h"
#include "World/Entity.h"
#include "World/Components/Camera.h"
#include "Input/Input.h"
#include "Commands/CommandStack.h"
#include "Commands/CommandTransform.h"
#include "Commands/CommandTransformMulti.h"
#include "Engine.h"
#include <vector>
//=========================================

namespace ImGui::TransformGizmo
{
    const  spartan::math::Vector3 snap = spartan::math::Vector3(0.1f, 0.1f, 0.1f);

    bool first_use = true;
    std::vector<spartan::Entity*> entities_being_transformed;
    std::vector<spartan::math::Vector3> positions_previous;
    std::vector<spartan::math::Quaternion> rotations_previous;
    std::vector<spartan::math::Vector3> scales_previous;
    
    // for backwards compatibility with single entity
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

        const float line_thickness       = 8.0f;
        style.CenterCircleSize           = 5.0f;
        style.TranslationLineThickness   = line_thickness;
        style.TranslationLineArrowSize   = 6.0f;
        style.RotationLineThickness      = line_thickness;
        style.RotationOuterLineThickness = 2.0f;
        style.ScaleLineThickness         = line_thickness;
        style.ScaleLineCircleSize        = 7.0f;
    }

    static spartan::math::Matrix create_row_major_matrix(const spartan::math::Vector3& position, const spartan::math::Quaternion& rotation, const spartan::math::Vector3& scale)
    {
        const spartan::math::Matrix rotation_matrix = spartan::math::Matrix::CreateRotation(rotation).Transposed();

        return spartan::math::Matrix
        (
            scale.x * rotation_matrix.m00, scale.y * rotation_matrix.m01, scale.z * rotation_matrix.m02, position.x,
            scale.x * rotation_matrix.m10, scale.y * rotation_matrix.m11, scale.z * rotation_matrix.m12, position.y,
            scale.x * rotation_matrix.m20, scale.y * rotation_matrix.m21, scale.z * rotation_matrix.m22, position.z,
            0.0f,                    0.0f                   , 0.0f,                    1.0f
        );
    }

    static void tick()
    {
        if (spartan::Engine::IsFlagSet(spartan::EngineMode::Playing))
            return;

        spartan::Camera* camera = spartan::World::GetCamera();
        if (!camera)
            return;

        // get selected entities
        const std::vector<spartan::Entity*>& selected_entities = camera->GetSelectedEntities();
        if (selected_entities.empty())
            return;
            
        // use the first entity as the primary for rotation/scale reference
        spartan::Entity* primary_entity = selected_entities[0];
        if (!primary_entity)
            return;

        // switch between position, rotation and scale operations, with W, E and R respectively
        static ImGuizmo::OPERATION transform_operation = ImGuizmo::TRANSLATE;
        if (!camera->GetFlag(spartan::CameraFlags::IsControlled))
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

        // calculate center position of all selected entities for gizmo placement
        static bool use_world_space = true;
        spartan::math::Vector3 center_position = spartan::math::Vector3::Zero;
        uint32_t valid_entity_count = 0;
        for (spartan::Entity* entity : selected_entities)
        {
            if (entity)
            {
                center_position += use_world_space ? entity->GetPosition() : entity->GetPositionLocal();
                valid_entity_count++;
            }
        }
        if (valid_entity_count > 0)
        {
            center_position /= static_cast<float>(valid_entity_count);
        }
        
        // use center position for gizmo, but primary entity's rotation/scale for orientation
        spartan::math::Vector3 position        = center_position;
        spartan::math::Quaternion rotation     = use_world_space ? primary_entity->GetRotation() : primary_entity->GetRotationLocal();
        spartan::math::Vector3 scale           = use_world_space ? primary_entity->GetScale() : primary_entity->GetScaleLocal();
        spartan::math::Matrix transform_matrix = create_row_major_matrix(position, rotation, scale);
        
        // save the initial position for delta calculation
        spartan::math::Vector3 initial_position = position;
        spartan::math::Quaternion initial_rotation = rotation;
        spartan::math::Vector3 initial_scale = scale;

        // set viewport rectangle
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(), ImGui::GetWindowHeight());
        ImGuizmo::Manipulate(
            &matrix_view.m00,
            &matrix_projection.m00,
            transform_operation,
            use_world_space ? ImGuizmo::WORLD : ImGuizmo::LOCAL,
            &transform_matrix.m00,
            nullptr,
            &snap.x
        );

        // map imguizmo to transform
        if (ImGuizmo::IsUsing())
        {
            // start of handling - save the initial transforms for all entities
            if (first_use)
            {
                entities_being_transformed.clear();
                positions_previous.clear();
                rotations_previous.clear();
                scales_previous.clear();
                
                for (spartan::Entity* entity : selected_entities)
                {
                    if (entity)
                    {
                        entities_being_transformed.push_back(entity);
                        positions_previous.push_back(use_world_space ? entity->GetPosition() : entity->GetPositionLocal());
                        rotations_previous.push_back(use_world_space ? entity->GetRotation() : entity->GetRotationLocal());
                        scales_previous.push_back(use_world_space ? entity->GetScale() : entity->GetScaleLocal());
                    }
                }
                first_use = false;
            }

            transform_matrix.Transposed().Decompose(scale, rotation, position);
            
            // calculate deltas from primary entity
            spartan::math::Vector3 position_delta = position - initial_position;
            spartan::math::Quaternion rotation_delta = rotation * initial_rotation.Inverse();
            spartan::math::Vector3 scale_ratio = spartan::math::Vector3(
                initial_scale.x != 0.0f ? scale.x / initial_scale.x : 1.0f,
                initial_scale.y != 0.0f ? scale.y / initial_scale.y : 1.0f,
                initial_scale.z != 0.0f ? scale.z / initial_scale.z : 1.0f
            );
            
            // apply transforms to all selected entities
            for (spartan::Entity* entity : selected_entities)
            {
                if (!entity)
                    continue;
                    
                if (use_world_space)
                {
                    // for translation, apply the delta
                    entity->SetPosition(entity->GetPosition() + position_delta);
                    
                    // for rotation, apply the rotation delta
                    if (transform_operation == ImGuizmo::ROTATE)
                    {
                        entity->SetRotation(rotation_delta * entity->GetRotation());
                    }
                    
                    // for scale, apply the ratio
                    if (transform_operation == ImGuizmo::SCALE)
                    {
                        spartan::math::Vector3 current_scale = entity->GetScale();
                        entity->SetScale(spartan::math::Vector3(
                            current_scale.x * scale_ratio.x,
                            current_scale.y * scale_ratio.y,
                            current_scale.z * scale_ratio.z
                        ));
                    }
                }
                else
                {
                    entity->SetPositionLocal(entity->GetPositionLocal() + position_delta);
                    
                    if (transform_operation == ImGuizmo::ROTATE)
                    {
                        entity->SetRotationLocal(rotation_delta * entity->GetRotationLocal());
                    }
                    
                    if (transform_operation == ImGuizmo::SCALE)
                    {
                        spartan::math::Vector3 current_scale = entity->GetScaleLocal();
                        entity->SetScaleLocal(spartan::math::Vector3(
                            current_scale.x * scale_ratio.x,
                            current_scale.y * scale_ratio.y,
                            current_scale.z * scale_ratio.z
                        ));
                    }
                }
            }

            // end of handling - add transforms to the command stack for all entities as a single undo operation
            if (spartan::Input::GetKeyUp(spartan::KeyCode::Click_Left))
            {
                if (!entities_being_transformed.empty())
                {
                    spartan::CommandStack::Add<spartan::CommandTransformMulti>(entities_being_transformed, positions_previous, rotations_previous, scales_previous);
                }
                first_use = true;
            }
        }
    }

    static bool allow_picking()
    {
        return !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
    }
}
