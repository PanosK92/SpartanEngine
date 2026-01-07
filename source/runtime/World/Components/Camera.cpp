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

//= INCLUDES ========================
#include "pch.h"
#include "Camera.h"
#include "Renderable.h"
#include "Window.h"
#include "Physics.h"
#include "Light.h"
#include "../Entity.h"
#include "../../Input/Input.h"
#include "../../Rendering/Renderer.h"
#include "../../Display/Display.h"
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//===================================

//= NAMESPACES ===============
using namespace spartan::math;
using namespace std;
//============================

namespace spartan
{
    Camera::Camera(Entity* entity) : Component(entity)
    {
        m_entity_ptr->SetPosition(Vector3(0.0f, 3.0f, -5.0f));
        SetFlag(CameraFlags::CanBeControlled, true);
        SetFlag(CameraFlags::PhysicalBodyAnimation, true);
        m_pick_hits.reserve(256);
        m_pick_indices.reserve(65536);
        m_pick_vertices.reserve(65536);
    }

    void Camera::Initialize()
    {
        Component::Initialize();
        ComputeMatrices();
    }

    void Camera::Tick()
    {
        const auto& current_viewport = Renderer::GetViewport();
        if (m_last_known_viewport != current_viewport)
        {
            m_last_known_viewport = current_viewport;
            SetFlag(CameraFlags::IsDirty, true);
        }

        if (m_position != GetEntity()->GetPosition() || m_rotation != GetEntity()->GetRotation())
        {
            m_position = GetEntity()->GetPosition();
            m_rotation = GetEntity()->GetRotation();
            SetFlag(CameraFlags::IsDirty, true);
        }

        ProcessInput();
        ComputeMatrices();
    }

    void Camera::Save(pugi::xml_node& node)
    {
        node.append_attribute("aperture")       = m_aperture;
        node.append_attribute("shutter_speed")  = m_shutter_speed;
        node.append_attribute("iso")            = m_iso;
        node.append_attribute("fov_horizontal") = m_fov_horizontal_rad;
        node.append_attribute("near_plane")     = m_near_plane;
        node.append_attribute("far_plane")      = m_far_plane;
        node.append_attribute("projection")     = static_cast<int>(m_projection_type);
        node.append_attribute("flags")          = m_flags;
    }
    
    void Camera::Load(pugi::xml_node& node)
    {
        m_aperture           = node.attribute("aperture").as_float(5.6f);
        m_shutter_speed      = node.attribute("shutter_speed").as_float(1.0f / 125.0f);
        m_iso                = node.attribute("iso").as_float(200.0f);
        m_fov_horizontal_rad = node.attribute("fov_horizontal").as_float(90.0f * math::deg_to_rad);
        m_near_plane         = node.attribute("near_plane").as_float(0.1f);
        m_far_plane          = node.attribute("far_plane").as_float(10'000.0f);
        m_projection_type    = static_cast<ProjectionType>(node.attribute("projection").as_int(static_cast<int>(Projection_Perspective)));
        m_flags              = node.attribute("flags").as_uint(0);

        ComputeMatrices();
    }

    void Camera::SetProjection(const ProjectionType projection)
    {
        m_projection_type = projection;
        SetFlag(CameraFlags::IsDirty, true);
    }

    float Camera::GetFovHorizontalDeg() const
    {
        return m_fov_horizontal_rad * math::rad_to_deg;
    }

    float Camera::GetFovVerticalRad() const
    {
        return 2.0f * atan(tan(m_fov_horizontal_rad / 2.0f) * (Renderer::GetViewport().height / Renderer::GetViewport().width));
    }

    void Camera::SetFovHorizontalDeg(const float fov)
    {
        m_fov_horizontal_rad = fov * math::deg_to_rad;
        SetFlag(CameraFlags::IsDirty, true);
    }

    float Camera::GetAspectRatio() const
    {
        return Renderer::GetViewport().GetAspectRatio();
    }

    bool Camera::IsInViewFrustum(const BoundingBox& bounding_box) const
    {
        const Vector3 center  = bounding_box.GetCenter();
        const Vector3 extents = bounding_box.GetExtents();

        return m_frustum.IsVisible(center, extents);
    }

    bool Camera::IsInViewFrustum(shared_ptr<Renderable> renderable) const
    {
        const BoundingBox& box = renderable->GetBoundingBox();
        return IsInViewFrustum(box);
    }

    const Ray& Camera::ComputePickingRay()
    {
        static Ray ray;

        ray.m_origin    = GetEntity()->GetPosition();
        ray.m_direction = ScreenToWorldCoordinates(Input::GetMousePositionRelativeToEditorViewport(), 1.0f);

        return ray;
    }
    
    void Camera::Pick()
    {
        if (!Input::GetMouseIsInViewport())
        {
            ClearSelection();
            return;
        }

        const Ray& ray = ComputePickingRay();
        m_pick_hits.clear();

        const vector<Entity*>& entities = World::GetEntities();
        for (Entity* entity : entities)
        {
            if (!entity->GetComponent<Renderable>())
                continue;

            const BoundingBox& aabb = entity->GetComponent<Renderable>()->GetBoundingBox();
            float distance          = ray.HitDistance(aabb);
            if (distance == numeric_limits<float>::infinity())
                continue;

            m_pick_hits.emplace_back(entity, Vector3::Zero, distance, distance == 0.0f);
        }

        if (m_pick_hits.empty())
        {
            ClearSelection();
            return;
        }

        Vector2 cursor         = Input::GetMousePosition();
        float best_screen_dist = numeric_limits<float>::max();
        float best_depth       = numeric_limits<float>::max();
        Entity* best_entity    = nullptr;
        for (RayHitResult& broad_hit : m_pick_hits)
        {
            Renderable* renderable = broad_hit.m_entity->GetComponent<Renderable>();

            // query mesh size first to reserve exact capacity and avoid allocations
            uint32_t index_count  = renderable->GetIndexCount();
            uint32_t vertex_count = renderable->GetVertexCount();
            
            // reserve exact capacity needed to avoid heap allocations in GetGeometry::resize()
            // only reserve if current capacity is insufficient
            if (m_pick_indices.capacity() < index_count)
            {
                m_pick_indices.reserve(index_count);
            }
            if (m_pick_vertices.capacity() < vertex_count)
            {
                m_pick_vertices.reserve(vertex_count);
            }
            
            // clear and reuse pre-allocated buffers 
            m_pick_indices.clear();
            m_pick_vertices.clear();

            renderable->GetGeometry(&m_pick_indices, &m_pick_vertices);
            if (m_pick_indices.empty() || m_pick_vertices.empty())
                continue;

            const Matrix& transform = broad_hit.m_entity->GetMatrix();

            for (uint32_t i = 0; i < m_pick_indices.size(); i += 3)
            {
                Vector3 p1(m_pick_vertices[m_pick_indices[i]].pos);
                Vector3 p2(m_pick_vertices[m_pick_indices[i + 1]].pos);
                Vector3 p3(m_pick_vertices[m_pick_indices[i + 2]].pos);

                p1 = p1 * transform;
                p2 = p2 * transform;
                p3 = p3 * transform;

                float distance = ray.HitDistance(p1, p2, p3);
                if (distance == numeric_limits<float>::infinity())
                    continue;

                Vector3 world_hit = ray.GetStart() + ray.GetDirection() * distance;

                // project to clip space
                Vector4 clip = Vector4(world_hit, 1.0f) * GetViewProjectionMatrix();
                if (clip.w == 0.0f)
                    continue;

                // ndc → screen
                Vector2 screen_pos(
                    (clip.x / clip.w * 0.5f + 0.5f) * Renderer::GetViewport().width,
                    (clip.y / clip.w * 0.5f + 0.5f) * Renderer::GetViewport().height
                );

                float screen_dist = (screen_pos - cursor).Length();

                // prefer smallest screen distance, then depth
                if (screen_dist < best_screen_dist || (screen_dist == best_screen_dist && distance < best_depth))
                {
                    best_screen_dist = screen_dist;
                    best_depth       = distance;
                    best_entity      = broad_hit.m_entity;
                }
            }
        }

        // handle ctrl for multi-select
        if (best_entity)
        {
            if (Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right))
            {
                ToggleSelection(best_entity);
            }
            else
            {
                SetSelectedEntity(best_entity);
            }
        }
        else
        {
            if (!(Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right)))
            {
                ClearSelection();
            }
        }
    }
    
    void Camera::SetSelectedEntity(Entity* entity)
    {
        m_selected_entities.clear();
        if (entity)
        {
            m_selected_entities.push_back(entity);
        }
    }
    
    Entity* Camera::GetSelectedEntity()
    {
        return m_selected_entities.empty() ? nullptr : m_selected_entities[0];
    }
    
    void Camera::AddToSelection(Entity* entity)
    {
        if (!entity)
            return;
        
        // check if already selected
        for (Entity* e : m_selected_entities)
        {
            if (e && e->GetObjectId() == entity->GetObjectId())
                return;
        }
        
        m_selected_entities.push_back(entity);
    }
    
    void Camera::RemoveFromSelection(Entity* entity)
    {
        if (!entity)
            return;
        
        m_selected_entities.erase(
            remove_if(m_selected_entities.begin(), m_selected_entities.end(),
                [entity](Entity* e) { return e && e->GetObjectId() == entity->GetObjectId(); }),
            m_selected_entities.end()
        );
    }
    
    void Camera::ToggleSelection(Entity* entity)
    {
        if (!entity)
            return;
        
        if (IsSelected(entity))
        {
            RemoveFromSelection(entity);
        }
        else
        {
            AddToSelection(entity);
        }
    }
    
    void Camera::ClearSelection()
    {
        m_selected_entities.clear();
    }
    
    bool Camera::IsSelected(Entity* entity) const
    {
        if (!entity)
            return false;
        
        for (Entity* e : m_selected_entities)
        {
            if (e && e->GetObjectId() == entity->GetObjectId())
                return true;
        }
        return false;
    }

    void Camera::WorldToScreenCoordinates(const Vector3& position_world, Vector2& position_screen) const
    {
        const Vector3 position_clip = position_world * m_view_projection_non_reverse_z;

        // convert clip space position to screen space position
        const RHI_Viewport& viewport = Renderer::GetViewport();
        float viewport_half_width    = viewport.width  * 0.5f;
        float viewport_half_height   = viewport.height * 0.5f;
        position_screen.x            = (position_clip.x / position_clip.z) *  viewport_half_width  + viewport_half_width;
        position_screen.y            = (position_clip.y / position_clip.z) * -viewport_half_height + viewport_half_height;
    }

    Rectangle Camera::WorldToScreenCoordinates(const BoundingBox& bounding_box) const
    {
        const Vector3& min = bounding_box.GetMin();
        const Vector3& max = bounding_box.GetMax();

        Vector3 corners[8];
        corners[0] = min;
        corners[1] = Vector3(max.x, min.y, min.z);
        corners[2] = Vector3(min.x, max.y, min.z);
        corners[3] = Vector3(max.x, max.y, min.z);
        corners[4] = Vector3(min.x, min.y, max.z);
        corners[5] = Vector3(max.x, min.y, max.z);
        corners[6] = Vector3(min.x, max.y, max.z);
        corners[7] = max;

        math::Rectangle rectangle_screen_Space;
        Vector2 position_screen_space;
        for (Vector3& corner : corners)
        {
            WorldToScreenCoordinates(corner, position_screen_space);
            rectangle_screen_Space.Merge(position_screen_space);
        }

        return rectangle_screen_Space;
    }

    Vector3 Camera::ScreenToWorldCoordinates(const Vector2& position_screen, const float z) const
    {
        Vector3 position_clip;
        const RHI_Viewport& viewport = Renderer::GetViewport();
        position_clip.x              = (position_screen.x / viewport.width) * 2.0f - 1.0f;
        position_clip.y              = (position_screen.y / viewport.height) * -2.0f + 1.0f;
        position_clip.z              = clamp(z, 0.0f, 1.0f);

        // compute world space position
        Matrix view_projection_inverted = m_view_projection_non_reverse_z.Inverted();
        Vector4 position_world          = Vector4(position_clip, 1.0f) * view_projection_inverted;

        return Vector3(position_world) / position_world.w;
    }

    void Camera::ComputeMatrices()
    {
        if (!GetFlag(CameraFlags::IsDirty))
            return;

        m_view                          = UpdateViewMatrix();
        m_projection                    = ComputeProjection(m_far_plane, m_near_plane);
        m_projection_non_reverse_z      = ComputeProjection(m_near_plane, m_far_plane);
        m_view_projection               = m_view * m_projection;
        m_view_projection_non_reverse_z = m_view * m_projection_non_reverse_z;
        m_frustum                       = Frustum(GetViewMatrix(), GetProjectionMatrix());
        SetFlag(CameraFlags::IsDirty, false);
    }

    void Camera::ProcessInput()
    {
        if (GetFlag(CameraFlags::CanBeControlled))
        {
            Input_FpsControl();
        }

        // shortcuts
        {
            Input_LerpToEntity(); // f
        }
    }

    void Camera::Input_FpsControl()
    {
        // parameters
        static const float jump_height       = 2.0f;  // target height in meters
        static const float jump_acceleration = 20.0f; // acceleration to reach height (m/s^2)
        static const float max_speed         = 5.0f;  // maximum movement speed
        static const float acceleration      = 1.0f;  // speed increase per second
        static const float drag              = 10.0f; // speed decrease per second
        float delta_time                     = static_cast<float>(Timer::GetDeltaTimeSec());

        // input mapping
        bool button_move_forward    = Input::GetKey(KeyCode::W);
        bool button_move_backward   = Input::GetKey(KeyCode::S);
        bool button_move_right      = Input::GetKey(KeyCode::D);
        bool button_move_left       = Input::GetKey(KeyCode::A);
        bool button_move_up         = Input::GetKey(KeyCode::Q);
        bool button_move_down       = Input::GetKey(KeyCode::E);
        bool button_sprint          = Input::GetKey(KeyCode::Shift_Left) || Input::GetKey(KeyCode::Left_Shoulder);
        bool button_jump            = Input::GetKeyDown(KeyCode::Space) || Input::GetKeyDown(KeyCode::Button_South);
        bool button_crouch          = Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Button_East); // Left Ctrl or O button
        bool button_flashlight      = Input::GetKeyDown(KeyCode::F) || Input::GetKeyDown(KeyCode::Button_North);
        bool mouse_click_right_down = Input::GetKeyDown(KeyCode::Click_Right);
        bool mouse_click_right      = Input::GetKey(KeyCode::Click_Right);
        bool mouse_click_left_down  = Input::GetKeyDown(KeyCode::Click_Left);

        // if the camera is paranted to an entity with a physics body, we will control that instead
        Physics* physics_body = nullptr;
        if (Entity* parent = GetEntity()->GetParent())
        {
            if (Physics* physics = parent->GetComponent<Physics>())
            {
                physics_body = physics;
            }
        }

        // deduce all states into booleans (some states exists as part of the class, so no need to deduce here)
        bool mouse_in_viewport    = Input::GetMouseIsInViewport();
        bool is_controlled        = GetFlag(CameraFlags::IsControlled);
        bool wants_cursor_hidden  = GetFlag(CameraFlags::WantsCursorHidden);
        bool is_gamepad_connected = Input::IsGamepadConnected();
        bool is_playing           = Engine::IsFlagSet(EngineMode::Playing);
        bool has_physics_body     = physics_body != nullptr;
        bool is_grounded          = has_physics_body ? physics_body->IsGrounded() : false;
        bool is_crouching         = button_crouch && is_grounded;
        m_is_walking              = (button_move_forward || button_move_backward || button_move_left || button_move_right) && is_grounded;
        
        // behavior: control activation and cursor handling
        {
            bool control_initiated  = mouse_click_right_down && mouse_in_viewport;
            bool control_maintained = mouse_click_right && is_controlled;
            bool is_controlled_new  = control_initiated || control_maintained;
            SetFlag(CameraFlags::IsControlled, is_controlled_new);
    
            if (is_controlled_new && !wants_cursor_hidden)
            {
                m_mouse_last_position = Input::GetMousePosition();
                if (!Window::IsFullScreen())
                {
                    Input::SetMouseCursorVisible(false);
                }
                SetFlag(CameraFlags::WantsCursorHidden, true);
            }
            else if (!is_controlled_new && wants_cursor_hidden)
            {
                Input::SetMousePosition(m_mouse_last_position);
                if (!Window::IsFullScreen())
                {
                    Input::SetMouseCursorVisible(true);
                }
                SetFlag(CameraFlags::WantsCursorHidden, false);
            }
        }
    
        // behavior: mouse look and movement direction calculation
        Vector3 movement_direction = Vector3::Zero;
        if (is_controlled || is_gamepad_connected)
        {
            // cursor edge wrapping
            if (is_controlled)
            {
                Vector2 mouse_pos = Input::GetMousePosition();
                uint32_t edge = 5;
                if (mouse_pos.x >= Display::GetWidth() - edge)
                {
                    Input::SetMousePosition(Vector2(static_cast<float>(edge + 1), mouse_pos.y));
                }
                else if (mouse_pos.x <= edge)
                {
                    Input::SetMousePosition(Vector2(static_cast<float>(Display::GetWidth() - edge - 1), mouse_pos.y));
                }
            }
    
            // mouse and gamepad look
            Quaternion current_rotation = GetEntity()->GetRotation();
            Vector2 input_delta = Vector2::Zero;
            if (is_controlled)
            {
                input_delta = Input::GetMouseDelta() * m_mouse_sensitivity;
            }
            else if (is_gamepad_connected)
            {
                input_delta = Input::GetGamepadThumbStickRight();
            }
            Quaternion yaw_increment   = Quaternion::FromAxisAngle(Vector3::Up, input_delta.x * deg_to_rad);
            Quaternion pitch_increment = Quaternion::FromAxisAngle(Vector3::Right, input_delta.y * deg_to_rad);
            Quaternion new_rotation    = yaw_increment * current_rotation * pitch_increment;
            Vector3 forward            = new_rotation * Vector3::Forward;
            float pitch_angle          = asin(-forward.y) * rad_to_deg;
            if (pitch_angle > 80.0f || pitch_angle < -80.0f)
            {
                new_rotation = yaw_increment * current_rotation;
            }
            GetEntity()->SetRotationLocal(new_rotation.Normalized());
    
            // Keyboard and gamepad movement direction
            if (is_controlled)
            {
                if (button_move_forward)  movement_direction += GetEntity()->GetForward();
                if (button_move_backward) movement_direction += GetEntity()->GetBackward();
                if (button_move_right)    movement_direction += GetEntity()->GetRight();
                if (button_move_left)     movement_direction += GetEntity()->GetLeft();
                if (button_move_up)       movement_direction += Vector3::Up;
                if (button_move_down)     movement_direction += Vector3::Down;
            }
            else if (is_gamepad_connected)
            {
                movement_direction += GetEntity()->GetBackward() * Input::GetGamepadThumbStickLeft().y;
                movement_direction += GetEntity()->GetRight()    * Input::GetGamepadThumbStickLeft().x;
                movement_direction += Vector3::Up                * Input::GetGamepadTriggerRight();
                movement_direction += Vector3::Down              * Input::GetGamepadTriggerLeft();
            }
    
            if (has_physics_body && is_playing)
            {
                movement_direction.y = 0.0f;
            }
            movement_direction.Normalize();
        }
    
        // behavior: speed adjustment
        {
            m_movement_scroll_accumulator += Input::GetMouseWheelDelta().y * 0.1f;
            m_movement_scroll_accumulator = clamp(m_movement_scroll_accumulator, -acceleration + 0.1f, acceleration * 2.0f);
    
            Vector3 translation = (acceleration + m_movement_scroll_accumulator) * movement_direction * 4.0f;
            if (button_sprint)
            {
                translation *= 3.0f;
            }
            m_movement_speed += translation * delta_time;
            m_movement_speed *= clamp(1.0f - drag * delta_time, 0.1f, numeric_limits<float>::max());
            if (m_movement_speed.Length() > max_speed)
            {
                m_movement_speed = m_movement_speed.Normalized() * max_speed;
            }
        }
    
        // behavior: physical body animation
        if (GetFlag(CameraFlags::PhysicalBodyAnimation) && is_playing && has_physics_body && is_grounded)
        {
            static Vector3 base_local_position = GetEntity()->GetPositionLocal();
            static Vector3 bob_offset          = Vector3::Zero;
            static float bob_timer             = 0.0f;
            static float breathe_timer         = 0.0f;
    
            float velocity_magnitude = physics_body->GetLinearVelocity().Length();
            if (velocity_magnitude > 0.01f) // walking head bob
            {
                bob_timer           += delta_time * velocity_magnitude * 1.5f;
                float bob_amplitude  = 0.04f;
                bob_offset.y         = sin(bob_timer) * bob_amplitude;
                bob_offset.x         = cos(bob_timer) * bob_amplitude * 0.5f;
            }
            else // breathing effect when resting
            {
                breathe_timer               += delta_time * 2.0f;
                float breathe_amplitude      = 0.0025f;
                float pitch_offset           = sin(breathe_timer) * breathe_amplitude;
                Quaternion breathe_rotation  = Quaternion::FromAxisAngle(Vector3::Right, pitch_offset * deg_to_rad);
                Quaternion current_rotation  = GetEntity()->GetRotationLocal();
                GetEntity()->SetRotationLocal(current_rotation * breathe_rotation);
            }
            GetEntity()->SetPositionLocal(base_local_position + bob_offset);
        }
    
        // behavior: jumping
        {
            if (has_physics_body && is_playing && is_grounded && button_jump)
            {
                m_jump_velocity = sqrt(2.0f * jump_acceleration * jump_height); // initial velocity from v^2 = 2*a*h
                m_jump_time     = 0.0f;
            }
        
            if (m_jump_velocity > 0.0f)
            {
                m_jump_time         += delta_time;
                float max_jump_time  = m_jump_velocity / jump_acceleration; // time to peak from v = a*t
                if (m_jump_time <= max_jump_time)
                {
                    Vector3 displacement = Vector3(0.0f, m_jump_velocity * delta_time, 0.0f);
                    physics_body->Move(displacement);
                }
                else
                {
                    m_jump_velocity = 0.0f; // stop applying upward velocity
                }
            }
        
            // end jump condition
            if (is_grounded && m_jump_velocity == 0.0f)
            {
                m_jump_time = 0.0f;
            }
        }

        // behavior: crouching
        if (has_physics_body && is_playing)
        {
            physics_body->Crouch(is_crouching);
        }
        
        // behavior: apply movement
        if (m_movement_speed != Vector3::Zero || (has_physics_body && is_playing && is_grounded))
        {
            if (has_physics_body && is_playing)
            {
                if (physics_body->GetBodyType() == BodyType::Controller)
                {
                    physics_body->Move(m_movement_speed * delta_time * 10.0f);
                }
                else if (is_grounded)
                {
                    Vector3 velocity        = physics_body->GetLinearVelocity();
                    Vector3 target_velocity = Vector3(m_movement_speed.x * 70.0f, velocity.y, m_movement_speed.z * 70.0f);
                    float force_multiplier  = 50.0f;
                    if (movement_direction.LengthSquared() < 0.1f)
                    {
                        force_multiplier *= 8.0f;
                    }
                    Vector3 force = (target_velocity - velocity) * force_multiplier;
                    physics_body->ApplyForce(force, PhysicsForce::Constant);
                }
            }
            else if (has_physics_body)
            {
                physics_body->Move(m_movement_speed);
            }
            else
            {
                GetEntity()->Translate(m_movement_speed);
            }
        }

        // behavior: flashlight
        {
            // create flashlight entity once
            if (!m_flashlight)
            {
                // entity
                m_flashlight = World::CreateEntity();
                m_flashlight->SetObjectName("flashlight");
                m_flashlight->SetParent(GetEntity());
                m_flashlight->SetRotationLocal(Quaternion::Identity);

                // component
                Light* light = m_flashlight->AddComponent<Light>();
                light->SetLightType(LightType::Spot);
                light->SetColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
                light->SetRange(100.0f);
                light->SetIntensity(2000.0f);
                light->SetAngle(30.0f * math::deg_to_rad);
                light->SetFlag(LightFlags::Volumetric, false);
                light->SetFlag(LightFlags::ShadowsScreenSpace, false);
                light->SetFlag(LightFlags::Shadows, true);
            }

            // toggle
            if (button_flashlight && is_playing)
            {
                SetFlag(CameraFlags::Flashlight, !GetFlag(CameraFlags::Flashlight));
            }

            // ensure flashlight follows camera and respects active state
            if (m_flashlight)
            {
                // ensure parent is set (in case camera entity was recreated)
                if (m_flashlight->GetParent() != GetEntity())
                {
                    m_flashlight->SetParent(GetEntity());
                    m_flashlight->SetRotationLocal(Quaternion::Identity);
                }

                // set active state and intensity based on flag
                bool flashlight_enabled = GetFlag(CameraFlags::Flashlight);
                m_flashlight->SetActive(flashlight_enabled);
                
                if (Light* light = m_flashlight->GetComponent<Light>())
                {
                    // set intensity to 0 when off, restore to 2000 when on
                    light->SetIntensity(flashlight_enabled ? 2000.0f : 0.0f);
                }
            }
        }

        // behaviour: shoot (physics boxes for now)
        if (mouse_click_left_down && mouse_click_right && mouse_in_viewport && is_playing)
        {
            // create entity and name it
            Entity* entity = World::CreateEntity();
            entity->SetObjectName("physics_box");

            // position it in front of the camera
            math::Vector3 spawn_offset = GetEntity()->GetForward() * 2.0f; // 2 meters ahead
            entity->SetPosition(GetEntity()->GetPosition() + spawn_offset);

            // give it a mesh and a material
            Renderable* renderable = entity->AddComponent<Renderable>();
            renderable->SetMesh(MeshType::Cube);
            renderable->SetDefaultMaterial();

            // add physics
            Physics* physics = entity->AddComponent<Physics>();
            physics->SetBodyType(BodyType::Box);
            physics->SetStatic(false);
            physics->SetKinematic(false);

            // apply bullet-like impulse
            float bullet_speed = 50.0f; // m/s
            physics->ApplyForce(GetEntity()->GetForward() * bullet_speed, PhysicsForce::Impulse);
        }
    }

    void Camera::Input_LerpToEntity()
    {
        // set focused entity as a lerp target
        if (Input::GetKeyDown(KeyCode::F))
        {
            FocusOnSelectedEntity();
        }

        // lerp
        if (m_lerp_to_target_p || m_lerp_to_target_r)
        {
            // lerp duration in seconds
            // 2.0 seconds + [0.0 - 2.0] seconds based on distance
            // Something is not right with the duration...
            const float lerp_duration = 2.0f + clamp(m_lerp_to_target_distance * 0.01f, 0.0f, 2.0f);

            // alpha
            m_lerp_to_target_alpha += static_cast<float>(Timer::GetDeltaTimeSec()) / lerp_duration;

            // position
            if (m_lerp_to_target_p)
            {
                const Vector3 interpolated_position = Vector3::Lerp(GetEntity()->GetPosition(), m_lerp_to_target_position, m_lerp_to_target_alpha);
                GetEntity()->SetPosition(interpolated_position);
            }

            // rotation
            if (m_lerp_to_target_r)
            {
                const Quaternion interpolated_rotation = Quaternion::Lerp(GetEntity()->GetRotation(), m_lerp_to_target_rotation, clamp(m_lerp_to_target_alpha, 0.0f, 1.0f));
                GetEntity()->SetRotation(interpolated_rotation);
            }

            // if the lerp has completed or the user has initiated fps control, stop lerping
            if (m_lerp_to_target_alpha >= 1.0f || GetFlag(CameraFlags::IsControlled))
            {
                m_lerp_to_target_p        = false;
                m_lerp_to_target_r        = false;
                m_lerp_to_target_alpha    = 0.0f;
                m_lerp_to_target_position = Vector3::Zero;
            }
        }
    }

    void Camera::FocusOnSelectedEntity()
    {
        // only do this in editor mode
        if (Engine::IsFlagSet(EngineMode::Playing))
            return;

        if (Entity* entity = GetSelectedEntity())
        {
            SP_LOG_INFO("Focusing on entity \"%s\"...", entity->GetObjectName().c_str());

            m_lerp_to_target_position = entity->GetPosition();
            const Vector3 target_direction = (m_lerp_to_target_position - GetEntity()->GetPosition()).Normalized();

            // if the entity has a renderable component, we can get a more accurate target position
            // ...otherwise we apply a simple offset so that the rotation vector doesn't suffer
            if (Renderable* renderable = entity->GetComponent<Renderable>())
            {
                m_lerp_to_target_position -= target_direction * renderable->GetBoundingBox().GetExtents().Length() * 2.0f;
            }
            else
            {
                m_lerp_to_target_position -= target_direction;
            }
            SP_ASSERT(!isnan(m_lerp_to_target_distance));

            m_lerp_to_target_rotation = Quaternion::FromLookRotation(entity->GetPosition() - m_lerp_to_target_position).Normalized();
            m_lerp_to_target_distance = Vector3::Distance(m_lerp_to_target_position, GetEntity()->GetPosition());

            const float lerp_angle = acosf(Quaternion::Dot(m_lerp_to_target_rotation.Normalized(), GetEntity()->GetRotation().Normalized())) * rad_to_deg;

            m_lerp_to_target_p = m_lerp_to_target_distance > 0.1f ? true : false;
            m_lerp_to_target_r = lerp_angle > 1.0f ? true : false;
        }
    }

    void Camera::SetFlag(const CameraFlags flag, const bool enable)
    {
        bool flag_present = m_flags & flag;

        if (enable && !flag_present)
        {
            m_flags |= static_cast<uint32_t>(flag);
        }
        else if (!enable && flag_present)
        {
            m_flags  &= ~static_cast<uint32_t>(flag);
        }
    }

    Matrix Camera::UpdateViewMatrix() const
    {
        Vector3 position = GetEntity()->GetPosition();
        Vector3 look_at  = GetEntity()->GetRotation() * Vector3::Forward;
        Vector3 up       = GetEntity()->GetRotation() * Vector3::Up;

        // offset look_at by current position
        look_at += position;

        // compute view matrix
        return Matrix::CreateLookAtLH(position, look_at, up);
    }

    Matrix Camera::ComputeProjection(const float near_plane, const float far_plane)
    {
        if (m_projection_type == Projection_Perspective)
        {
            return Matrix::CreatePerspectiveFieldOfViewLH(GetFovVerticalRad(), GetAspectRatio(), near_plane, far_plane);
        }
        else if (m_projection_type == Projection_Orthographic)
        {
            return Matrix::CreateOrthographicLH(Renderer::GetViewport().width, Renderer::GetViewport().height, near_plane, far_plane);
        }

        return Matrix::Identity;
    }
}
