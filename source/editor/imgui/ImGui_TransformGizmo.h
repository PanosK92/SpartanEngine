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
#include "../TransformGizmo.h"
#include "source/imgui.h"
#include "world/Entity.h"
#include "world/components/Camera.h"
#include "world/components/Render.h"
#include "input/Input.h"
#include "commands/CommandStack.h"
#include "commands/CommandTransform.h"
#include "commands/CommandTransformMulti.h"
#include "Engine.h"
#include "rendering/Renderer.h"
#include <vector>
#include <cfloat>
#include <cmath>
//=========================================

namespace ImGui::TransformGizmo
{
    const float snap_edge_threshold = 0.3f;

    inline ::TransformGizmo::Operation& operation()
    {
        static ::TransformGizmo::Operation value = ::TransformGizmo::Operation::Translate;
        return value;
    }

    inline ::TransformGizmo::Space& space()
    {
        static ::TransformGizmo::Space value = ::TransformGizmo::Space::Local;
        return value;
    }

    inline ::TransformGizmo::Pivot& pivot()
    {
        static ::TransformGizmo::Pivot value = ::TransformGizmo::Pivot::Median;
        return value;
    }

    inline void set_operation(::TransformGizmo::Operation op)
    {
        operation() = op;
    }

    inline void set_space(::TransformGizmo::Space s)
    {
        space() = s;
    }

    inline void toggle_space()
    {
        space() = (space() == ::TransformGizmo::Space::World)
            ? ::TransformGizmo::Space::Local
            : ::TransformGizmo::Space::World;
    }

    inline void set_pivot(::TransformGizmo::Pivot p)
    {
        pivot() = p;
    }

    inline bool first_use = true;
    inline bool style_applied = false;
    inline std::vector<spartan::Entity*> entities_being_transformed;
    inline std::vector<spartan::math::Vector3> positions_previous;
    inline std::vector<spartan::math::Quaternion> rotations_previous;
    inline std::vector<spartan::math::Vector3> scales_previous;

    inline void apply_style()
    {
        // slightly desaturated primaries, they read well against both bright and dark scenes
        const ImVec4 color_x = ImVec4(0.87f, 0.25f, 0.28f, 1.0f);
        const ImVec4 color_y = ImVec4(0.42f, 0.80f, 0.24f, 1.0f);
        const ImVec4 color_z = ImVec4(0.24f, 0.52f, 0.95f, 1.0f);

        ::TransformGizmo::Style& style = ::TransformGizmo::get_style();
        style.colors[static_cast<int>(::TransformGizmo::Color::DirectionX)] = color_x;
        style.colors[static_cast<int>(::TransformGizmo::Color::DirectionY)] = color_y;
        style.colors[static_cast<int>(::TransformGizmo::Color::DirectionZ)] = color_z;
        style.colors[static_cast<int>(::TransformGizmo::Color::PlaneX)]     = color_x;
        style.colors[static_cast<int>(::TransformGizmo::Color::PlaneY)]     = color_y;
        style.colors[static_cast<int>(::TransformGizmo::Color::PlaneZ)]     = color_z;
        style.colors[static_cast<int>(::TransformGizmo::Color::Selection)]  = ImVec4(1.00f, 0.80f, 0.20f, 1.0f);
        style.colors[static_cast<int>(::TransformGizmo::Color::Inactive)]   = ImVec4(0.55f, 0.55f, 0.58f, 0.55f);
        style.colors[static_cast<int>(::TransformGizmo::Color::Text)]       = ImVec4(0.94f, 0.95f, 0.97f, 1.0f);

        style.translation_line_thickness  = 4.0f;
        style.translation_line_arrow_size = 24.0f;
        style.rotation_line_thickness     = 3.5f;
        style.scale_line_thickness        = 4.0f;
        style.scale_line_circle_size      = 6.5f;
        style.center_circle_size          = 8.0f;
        style.plane_size                  = 0.30f;
        style.gizmo_size_clip_space       = 0.135f;
        style.show_delta_hud              = true;

        style.halo_thickness              = 8.0f;
        style.halo_alpha                  = 0.35f;
        style.ambient_glow                = 0.06f;
        style.glow_thickness              = 9.0f;
        style.hover_animation_speed       = 16.0f;
        style.inactive_dim                = 0.28f;
        style.show_halo                   = true;
        style.show_axis_labels            = true;
    }

    static spartan::math::Vector3 get_entity_position(spartan::Entity* entity, bool world)
    {
        return world ? entity->GetPosition() : entity->GetPositionLocal();
    }

    static spartan::math::Quaternion get_entity_rotation(spartan::Entity* entity, bool world)
    {
        return world ? entity->GetRotation() : entity->GetRotationLocal();
    }

    static spartan::math::Vector3 get_entity_scale(spartan::Entity* entity, bool world)
    {
        return world ? entity->GetScale() : entity->GetScaleLocal();
    }

    static void set_entity_position(spartan::Entity* entity, bool world, const spartan::math::Vector3& value)
    {
        if (world)
        {
            entity->SetPosition(value);
        }
        else
        {
            entity->SetPositionLocal(value);
        }
    }

    static void set_entity_rotation(spartan::Entity* entity, bool world, const spartan::math::Quaternion& value)
    {
        if (world)
        {
            entity->SetRotation(value);
        }
        else
        {
            entity->SetRotationLocal(value);
        }
    }

    static void set_entity_scale(spartan::Entity* entity, bool world, const spartan::math::Vector3& value)
    {
        if (world)
        {
            entity->SetScale(value);
        }
        else
        {
            entity->SetScaleLocal(value);
        }
    }

    static bool has_selected_ancestor(
        spartan::Entity* entity,
        const std::vector<spartan::Entity*>& selected_entities
    )
    {
        if (!entity)
        {
            return false;
        }

        for (spartan::Entity* ancestor = entity->GetParent(); ancestor; ancestor = ancestor->GetParent())
        {
            for (spartan::Entity* selected : selected_entities)
            {
                if (selected == ancestor)
                {
                    return true;
                }
            }
        }

        return false;
    }

    static void apply_aabb_edge_snap(
        spartan::Entity* primary_entity,
        const std::vector<spartan::Entity*>& selected_entities,
        const spartan::math::Vector3& initial_position,
        spartan::math::Vector3& position
    )
    {
        spartan::Render* primary_render = primary_entity->GetComponent<spartan::Render>();
        if (!primary_render)
        {
            return;
        }

        spartan::math::Vector3 tentative_delta    = position - initial_position;
        const spartan::math::BoundingBox& cur_box = primary_render->GetBoundingBox();
        spartan::math::Vector3 aabb_min           = cur_box.GetMin() + tentative_delta;
        spartan::math::Vector3 aabb_max           = cur_box.GetMax() + tentative_delta;

        float best_correction[3] = { 0.0f, 0.0f, 0.0f };
        float best_dist[3]       = { FLT_MAX, FLT_MAX, FLT_MAX };

        for (spartan::Entity* other : spartan::World::GetEntitiesWithRender())
        {
            bool is_selected = false;
            for (spartan::Entity* sel : selected_entities)
            {
                if (sel == other)
                {
                    is_selected = true;
                    break;
                }
            }
            if (is_selected)
            {
                continue;
            }

            spartan::Render* other_render = other->GetComponent<spartan::Render>();
            if (!other_render)
            {
                continue;
            }

            const spartan::math::BoundingBox& other_box = other_render->GetBoundingBox();
            float o_min[3] = { other_box.GetMin().x, other_box.GetMin().y, other_box.GetMin().z };
            float o_max[3] = { other_box.GetMax().x, other_box.GetMax().y, other_box.GetMax().z };
            float s_min[3] = { aabb_min.x, aabb_min.y, aabb_min.z };
            float s_max[3] = { aabb_max.x, aabb_max.y, aabb_max.z };

            for (int axis = 0; axis < 3; axis++)
            {
                int a1 = (axis + 1) % 3;
                int a2 = (axis + 2) % 3;
                bool overlap = s_min[a1] < o_max[a1] && s_max[a1] > o_min[a1]
                            && s_min[a2] < o_max[a2] && s_max[a2] > o_min[a2];
                if (!overlap)
                {
                    continue;
                }

                float cur_min = (&cur_box.GetMin().x)[axis];
                float cur_max = (&cur_box.GetMax().x)[axis];

                float d1 = fabsf(s_min[axis] - o_max[axis]);
                if (d1 < snap_edge_threshold && d1 < best_dist[axis])
                {
                    best_dist[axis]       = d1;
                    best_correction[axis] = o_max[axis] - cur_min;
                }

                float d2 = fabsf(s_max[axis] - o_min[axis]);
                if (d2 < snap_edge_threshold && d2 < best_dist[axis])
                {
                    best_dist[axis]       = d2;
                    best_correction[axis] = o_min[axis] - cur_max;
                }
            }
        }

        for (int axis = 0; axis < 3; axis++)
        {
            if (best_dist[axis] < snap_edge_threshold)
            {
                (&position.x)[axis] = (&initial_position.x)[axis] + best_correction[axis];
            }
        }
    }

    inline void tick()
    {
        if (spartan::Engine::IsFlagSet(spartan::EngineMode::Playing))
        {
            return;
        }

        spartan::Camera* camera = spartan::World::GetCamera();
        if (!camera)
        {
            return;
        }

        const std::vector<spartan::Entity*>& selected_entities = camera->GetSelectedEntities();
        if (selected_entities.empty())
        {
            return;
        }

        spartan::Entity* primary_entity = selected_entities[0];
        if (!primary_entity)
        {
            return;
        }

        if (!style_applied)
        {
            apply_style();
            style_applied = true;
        }

        const bool gizmo_keys_blocked =
            camera->GetFlag(spartan::CameraFlags::IsControlled) ||
            spartan::Engine::IsFlagSet(spartan::EngineMode::Playing) ||
            ImGui::GetIO().WantTextInput;

        if (!gizmo_keys_blocked)
        {
            if (spartan::Input::GetKeyDown(spartan::KeyCode::W))
            {
                operation() = ::TransformGizmo::Operation::Translate;
            }
            else if (spartan::Input::GetKeyDown(spartan::KeyCode::E))
            {
                operation() = ::TransformGizmo::Operation::Rotate;
            }
            else if (spartan::Input::GetKeyDown(spartan::KeyCode::R))
            {
                operation() = ::TransformGizmo::Operation::Scale;
            }
            else if (spartan::Input::GetKeyDown(spartan::KeyCode::T))
            {
                operation() = ::TransformGizmo::Operation::Universal;
            }
            else if (spartan::Input::GetKeyDown(spartan::KeyCode::X))
            {
                toggle_space();
            }
            else if (
                spartan::Input::GetKeyDown(spartan::KeyCode::Z) &&
                !spartan::Input::GetKey(spartan::KeyCode::Ctrl_Left) &&
                !spartan::Input::GetKey(spartan::KeyCode::Ctrl_Right)
            )
            {
                // cycle pivot: median -> active -> individual
                if (pivot() == ::TransformGizmo::Pivot::Median)
                {
                    pivot() = ::TransformGizmo::Pivot::Active;
                }
                else if (pivot() == ::TransformGizmo::Pivot::Active)
                {
                    pivot() = ::TransformGizmo::Pivot::Individual;
                }
                else
                {
                    pivot() = ::TransformGizmo::Pivot::Median;
                }
            }
        }

        const bool use_local = (space() == ::TransformGizmo::Space::Local);
        const ::TransformGizmo::Operation op = operation();
        const ::TransformGizmo::Pivot pivot_mode = pivot();

        // gizmo always sits in world space; local/world only changes axis orientation
        spartan::math::Vector3 gizmo_position = spartan::math::Vector3::Zero;
        if (pivot_mode == ::TransformGizmo::Pivot::Active || pivot_mode == ::TransformGizmo::Pivot::Individual)
        {
            gizmo_position = primary_entity->GetPosition();
        }
        else
        {
            uint32_t valid_entity_count = 0;
            for (spartan::Entity* entity : selected_entities)
            {
                if (entity)
                {
                    gizmo_position += entity->GetPosition();
                    valid_entity_count++;
                }
            }
            if (valid_entity_count > 0)
            {
                gizmo_position /= static_cast<float>(valid_entity_count);
            }
        }

        const spartan::math::Quaternion gizmo_rotation = use_local
            ? primary_entity->GetRotation()
            : spartan::math::Quaternion::Identity;
        const spartan::math::Vector3 gizmo_scale = spartan::math::Vector3::One;
        spartan::math::Matrix transform_matrix(gizmo_position, gizmo_rotation, gizmo_scale);

        const spartan::math::Vector3 initial_position = gizmo_position;
        const spartan::math::Quaternion initial_rotation = gizmo_rotation;
        const spartan::math::Vector3 initial_scale = gizmo_scale;

        const spartan::math::Matrix& matrix_view       = camera->GetViewMatrix();
        const spartan::math::Matrix& matrix_projection = camera->GetProjectionMatrix();

        ::TransformGizmo::set_orthographic(false);
        ::TransformGizmo::begin_frame();
        ::TransformGizmo::set_draw_list(ImGui::GetWindowDrawList());
        ::TransformGizmo::set_rect(
            ImGui::GetWindowPos().x,
            ImGui::GetWindowPos().y,
            ImGui::GetWindowWidth(),
            ImGui::GetWindowHeight()
        );

        const float snap_translate = spartan::cvar_transform_snap_translate.GetValueAs<float>();
        const float snap_rotate    = spartan::cvar_transform_snap_rotate.GetValueAs<float>();
        const float snap_scale     = spartan::cvar_transform_snap_scale.GetValueAs<float>();
        float snap_values[3] = { snap_translate, snap_translate, snap_translate };
        if (op == ::TransformGizmo::Operation::Rotate)
        {
            snap_values[0] = snap_values[1] = snap_values[2] = snap_rotate;
        }
        else if (op == ::TransformGizmo::Operation::Scale)
        {
            snap_values[0] = snap_values[1] = snap_values[2] = snap_scale;
        }

        const bool snap_enabled = spartan::cvar_transform_snap.GetValueAs<bool>();
        ::TransformGizmo::manipulate(
            &matrix_view.m00,
            &matrix_projection.m00,
            op,
            space(),
            &transform_matrix.m00,
            nullptr,
            snap_enabled ? snap_values : nullptr
        );

        if (::TransformGizmo::is_using())
        {
            if (first_use)
            {
                entities_being_transformed.clear();
                positions_previous.clear();
                rotations_previous.clear();
                scales_previous.clear();

                for (spartan::Entity* entity : selected_entities)
                {
                    if (entity && !has_selected_ancestor(entity, selected_entities))
                    {
                        entities_being_transformed.push_back(entity);
                        positions_previous.push_back(entity->GetPosition());
                        rotations_previous.push_back(entity->GetRotation());
                        scales_previous.push_back(entity->GetScale());
                    }
                }
                first_use = false;
            }

            spartan::math::Vector3 position;
            spartan::math::Quaternion rotation;
            spartan::math::Vector3 scale;
            transform_matrix.Decompose(scale, rotation, position);

            if (snap_enabled && op == ::TransformGizmo::Operation::Translate)
            {
                apply_aabb_edge_snap(primary_entity, selected_entities, initial_position, position);
            }

            spartan::math::Vector3 position_delta = position - initial_position;
            spartan::math::Quaternion rotation_delta = rotation * initial_rotation.Inverse();
            spartan::math::Vector3 scale_ratio = spartan::math::Vector3(
                initial_scale.x != 0.0f ? scale.x / initial_scale.x : 1.0f,
                initial_scale.y != 0.0f ? scale.y / initial_scale.y : 1.0f,
                initial_scale.z != 0.0f ? scale.z / initial_scale.z : 1.0f
            );

            const bool do_translate = (op == ::TransformGizmo::Operation::Translate) ||
                (op == ::TransformGizmo::Operation::Universal && position_delta.Length() > 0.00001f);
            const bool do_rotate = (op == ::TransformGizmo::Operation::Rotate) ||
                (op == ::TransformGizmo::Operation::Universal && (1.0f - fabsf(rotation_delta.w)) > 0.00001f);
            const bool do_scale = (op == ::TransformGizmo::Operation::Scale) ||
                (op == ::TransformGizmo::Operation::Universal &&
                    (fabsf(scale_ratio.x - 1.0f) + fabsf(scale_ratio.y - 1.0f) + fabsf(scale_ratio.z - 1.0f)) > 0.00001f);

            for (spartan::Entity* entity : selected_entities)
            {
                if (!entity || has_selected_ancestor(entity, selected_entities))
                {
                    continue;
                }

                if (do_translate)
                {
                    entity->SetPosition(entity->GetPosition() + position_delta);
                }

                if (do_rotate)
                {
                    if (pivot_mode != ::TransformGizmo::Pivot::Individual)
                    {
                        const spartan::math::Vector3 relative = entity->GetPosition() - initial_position;
                        const spartan::math::Vector3 rotated  = rotation_delta * relative;
                        entity->SetPosition(initial_position + rotated);
                    }
                    entity->SetRotation(rotation_delta * entity->GetRotation());
                }

                if (do_scale)
                {
                    const spartan::math::Vector3 current_scale = entity->GetScale();
                    entity->SetScale(spartan::math::Vector3(
                        current_scale.x * scale_ratio.x,
                        current_scale.y * scale_ratio.y,
                        current_scale.z * scale_ratio.z
                    ));
                }
            }

            if (spartan::Input::GetKeyUp(spartan::KeyCode::Click_Left))
            {
                if (!entities_being_transformed.empty())
                {
                    spartan::CommandStack::Add<spartan::CommandTransformMulti>(
                        entities_being_transformed,
                        positions_previous,
                        rotations_previous,
                        scales_previous
                    );
                }
                first_use = true;
            }
        }
        else
        {
            first_use = true;
        }
    }

    inline bool allow_picking()
    {
        return !::TransformGizmo::is_over() && !::TransformGizmo::is_using();
    }
}
