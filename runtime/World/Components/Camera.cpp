/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "PhysicsBody.h"
#include "../Entity.h"
#include "../World.h"
#include "../../Input/Input.h"
#include "../../IO/FileStream.h"
#include "../../Rendering/Renderer.h"
#include "../../Display/Display.h"
//===================================

//= NAMESPACES ===============
using namespace Spartan::Math;
using namespace std;
//============================

namespace Spartan
{
    Camera::Camera(weak_ptr<Entity> entity) : Component(entity)
    {
        m_entity_ptr->SetPosition(Vector3(0.0f, 3.0f, -5.0f));
    }

    void Camera::OnInitialize()
    {
        Component::OnInitialize();
        ComputeMatrices();
    }

    void Camera::OnTick()
    {
        const auto& current_viewport = Renderer::GetViewport();
        if (m_last_known_viewport != current_viewport)
        {
            m_last_known_viewport = current_viewport;
            m_is_dirty            = true;
        }

        // DIRTY CHECK
        if (m_position != GetEntity()->GetPosition() || m_rotation != GetEntity()->GetRotation())
        {
            m_position = GetEntity()->GetPosition();
            m_rotation = GetEntity()->GetRotation();
            m_is_dirty = true;
        }

        ProcessInput();
        ComputeMatrices();
    }

    void Camera::Serialize(FileStream* stream)
    {
        stream->Write(m_aperture);
        stream->Write(m_shutter_speed);
        stream->Write(m_iso);
        stream->Write(uint32_t(m_projection_type));
        stream->Write(m_fov_horizontal_rad);
        stream->Write(m_near_plane);
        stream->Write(m_far_plane);
    }

    void Camera::Deserialize(FileStream* stream)
    {
        stream->Read(&m_aperture);
        stream->Read(&m_shutter_speed);
        stream->Read(&m_iso);
        m_projection_type = ProjectionType(stream->ReadAs<uint32_t>());
        stream->Read(&m_fov_horizontal_rad);
        stream->Read(&m_near_plane);
        stream->Read(&m_far_plane);

        ComputeMatrices();
    }

    void Camera::SetNearPlane(const float near_plane)
    {
        float near_plane_limited = Helper::Max(near_plane, 0.01f);

        if (m_near_plane != near_plane_limited)
        {
            m_near_plane = near_plane_limited;
            m_is_dirty   = true;
        }
    }

    void Camera::SetFarPlane(const float far_plane)
    {
        m_far_plane = far_plane;
        m_is_dirty  = true;
    }

    void Camera::SetProjection(const ProjectionType projection)
    {
        m_projection_type = projection;
        m_is_dirty        = true;
    }

    float Camera::GetFovHorizontalDeg() const
    {
        return Helper::RadiansToDegrees(m_fov_horizontal_rad);
    }

    float Camera::GetFovVerticalRad() const
    {
        return 2.0f * atan(tan(m_fov_horizontal_rad / 2.0f) * (Renderer::GetViewport().height / Renderer::GetViewport().width));
    }

    void Camera::SetFovHorizontalDeg(const float fov)
    {
        m_fov_horizontal_rad = Helper::DegreesToRadians(fov);
        m_is_dirty           = true;
    }

    bool Camera::IsInViewFrustum(const BoundingBox& bounding_box) const
    {
        if (bounding_box == BoundingBox::Undefined)
        {
            SP_LOG_WARNING("Undefined bounding box, treating as outside of the view frustum");
            return false;
        }

        const Vector3 center  = bounding_box.GetCenter();
        const Vector3 extents = bounding_box.GetExtents();

        return m_frustum.IsVisible(center, extents);
    }

    bool Camera::IsInViewFrustum(shared_ptr<Renderable> renderable) const
    {
        BoundingBoxType type   = renderable->HasInstancing() ? BoundingBoxType::TransformedInstances : BoundingBoxType::Transformed;
        const BoundingBox& box = renderable->GetBoundingBox(type);

        return IsInViewFrustum(box);
    }

	const Math::Ray Camera::ComputePickingRay()
	{
        Vector3 ray_start     = GetEntity()->GetPosition();
        Vector3 ray_direction = ScreenToWorldCoordinates(Input::GetMousePositionRelativeToEditorViewport(), 1.0f);
        return Ray(ray_start, ray_direction);
	}

	void Camera::Pick()
    {
        // ensure the mouse is inside the viewport
        if (!Input::GetMouseIsInViewport())
        {
            m_selected_entity.reset();
            return;
        }

        m_ray = ComputePickingRay();

        // traces ray against all AABBs in the world
        vector<RayHit> hits;
        {
            const vector<shared_ptr<Entity>>& entities = World::GetAllEntities();
            for (const shared_ptr<Entity> entity : entities)
            {
                // Make sure there entity has a renderable
                if (!entity->GetComponent<Renderable>())
                    continue;

                // Get object oriented bounding box
                const BoundingBox& aabb = entity->GetComponent<Renderable>()->GetBoundingBox(BoundingBoxType::Transformed);

                // Compute hit distance
                float distance = m_ray.HitDistance(aabb);

                // Don't store hit data if there was no hit
                if (distance == Helper::INFINITY_)
                    continue;

                hits.emplace_back(
                    entity,                                             // Entity
                    m_ray.GetStart() + m_ray.GetDirection() * distance, // Position
                    distance,                                           // Distance
                    distance == 0.0f                                    // Inside
                );
            }

            // sort by distance (ascending)
            sort(hits.begin(), hits.end(), [](const RayHit& a, const RayHit& b) { return a.m_distance < b.m_distance; });
        }

        // check if there are any hits
        if (hits.empty())
        {
            m_selected_entity.reset();
            return;
        }

        // if there is a single hit, return that
        if (hits.size() == 1)
        {
            m_selected_entity = hits.front().m_entity;
            return;
        }

        // if there are more hits, perform triangle intersection
        float distance_min = numeric_limits<float>::max();
        for (RayHit& hit : hits)
        {
            // Get entity geometry
            shared_ptr<Renderable> renderable = hit.m_entity->GetComponent<Renderable>();
            vector<uint32_t> indicies;
            vector<RHI_Vertex_PosTexNorTan> vertices;
            renderable->GetGeometry(&indicies, &vertices);
            if (indicies.empty()|| vertices.empty())
            {
                SP_LOG_ERROR("Failed to get geometry of entity %s, skipping intersection test.");
                continue;
            }

            // Compute matrix which can transform vertices to view space
            Matrix vertex_transform = hit.m_entity->GetMatrix();

            // Go through each face
            for (uint32_t i = 0; i < indicies.size(); i += 3)
            {
                Vector3 p1_world = Vector3(vertices[indicies[i]].pos) * vertex_transform;
                Vector3 p2_world = Vector3(vertices[indicies[i + 1]].pos) * vertex_transform;
                Vector3 p3_world = Vector3(vertices[indicies[i + 2]].pos) * vertex_transform;

                float distance = m_ray.HitDistance(p1_world, p2_world, p3_world);

                if (distance < distance_min)
                {
                    m_selected_entity = hit.m_entity;
                    distance_min      = distance;
                }
            }
        }
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

        Math::Rectangle rectangle_screen_Space;
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
        if (!m_is_dirty)
            return;

        m_view                          = ComputeViewMatrix();
        m_projection                    = ComputeProjection(m_far_plane, m_near_plane);
        m_projection_non_reverse_z      = ComputeProjection(m_near_plane, m_far_plane);
        m_view_projection               = m_view * m_projection;
        m_view_projection_non_reverse_z = m_view * m_projection_non_reverse_z;
        m_frustum                       = Frustum(GetViewMatrix(), GetProjectionMatrix(), m_near_plane);
        m_is_dirty                      = false;
    }

    void Camera::ProcessInput()
    {
        // fps camera controls
        // x-axis movement: w, a, s, d
        // y-axis movement: q, e
        // mouse look: hold right click to enable
        if (m_first_person_control_enabled)
        {
            ProcessInputFpsControl();
        }

        // shortcuts
        {
            // focus on selected entity: f
            ProcessInputLerpToEntity();
        }
    }

    void Camera::ProcessInputFpsControl()
    {
        static const float movement_speed_max = 5.0f;
        static float movement_acceleration    = 1.0f;
        static const float movement_drag      = 10.0f;
        Vector3 movement_direction            = Vector3::Zero;
        float delta_time                      = static_cast<float>(Timer::GetDeltaTimeSec());

        // detect if fps control should be activated
        {
            // initiate control only when the mouse is within the viewport
            if (Input::GetKeyDown(KeyCode::Click_Right) && Input::GetMouseIsInViewport())
            {
                m_is_controlled_by_keyboard_mouse = true;
            }

            // maintain control as long as the right click is pressed and initial control has been given
            m_is_controlled_by_keyboard_mouse = Input::GetKey(KeyCode::Click_Right) && m_is_controlled_by_keyboard_mouse;
        }

        // cursor visibility and position
        {
            // when right clicking and moving the mouse over the viewport (hide the mouse)
            if (m_is_controlled_by_keyboard_mouse && !m_fps_control_cursor_hidden)
            {
                m_mouse_last_position = Input::GetMousePosition();

                if (!Window::IsFullScreen()) // change the mouse state only in editor mode
                {
                    Input::SetMouseCursorVisible(false);
                }

                m_fps_control_cursor_hidden = true;
            }
            // when releasing the rick click, make the mouse visible and set it to the last visible position
            else if (!m_is_controlled_by_keyboard_mouse && m_fps_control_cursor_hidden)
            {
                Input::SetMousePosition(m_mouse_last_position);

                if (!Window::IsFullScreen()) // change the mouse state only in editor mode
                {
                    Input::SetMouseCursorVisible(true);
                }

                m_fps_control_cursor_hidden = false;
            }
        }

        // mouse look
        if (m_is_controlled_by_keyboard_mouse || Input::IsGamepadConnected())
        {
            // wrap around left and right screen edges (to allow for infinite scrolling)
            if (m_is_controlled_by_keyboard_mouse)
            {
                uint32_t edge_padding = 5;
                Vector2 mouse_position = Input::GetMousePosition();
                if (mouse_position.x >= Display::GetWidth() - edge_padding)
                {
                    mouse_position.x = static_cast<float>(edge_padding + 1);
                    Input::SetMousePosition(mouse_position);
                }
                else if (mouse_position.x <= edge_padding)
                {
                    mouse_position.x = static_cast<float>(Spartan::Display::GetWidth() - edge_padding - 1);
                    Input::SetMousePosition(mouse_position);
                }
            }

            // get camera rotation
            if (m_is_controlled_by_keyboard_mouse)
            {
                m_first_person_rotation.x = GetEntity()->GetRotation().Yaw();
                m_first_person_rotation.y = GetEntity()->GetRotation().Pitch();

                // get mouse delta
                const Vector2 mouse_delta = Input::GetMouseDelta() * m_mouse_sensitivity;

                // lerp to it
                m_mouse_smoothed = Helper::Lerp(m_mouse_smoothed, mouse_delta, Helper::Saturate(1.0f - m_mouse_smoothing));

                // accumulate rotation
                m_first_person_rotation += m_mouse_smoothed;
            }
            else if (Input::IsGamepadConnected())
            {
                m_first_person_rotation.x += Input::GetGamepadThumbStickRight().x;
                m_first_person_rotation.y += Input::GetGamepadThumbStickRight().y;
            }

            // clamp rotation along the x-axis (but not exactly at 90 degrees, this is to avoid a gimbal lock)
            m_first_person_rotation.y = Helper::Clamp(m_first_person_rotation.y, -80.0f, 80.0f);

            // compute rotation
            const Quaternion xQuaternion = Quaternion::FromAngleAxis(m_first_person_rotation.x * Helper::DEG_TO_RAD, Vector3::Up);
            const Quaternion yQuaternion = Quaternion::FromAngleAxis(m_first_person_rotation.y * Helper::DEG_TO_RAD, Vector3::Right);
            const Quaternion rotation = xQuaternion * yQuaternion;

            // rotate
            GetEntity()->SetRotationLocal(rotation);
        }

        // directional movement
        if (m_is_controlled_by_keyboard_mouse || Input::IsGamepadConnected())
        {
            if (m_is_controlled_by_keyboard_mouse)
            {
                if (Input::GetKey(KeyCode::W)) movement_direction += GetEntity()->GetForward();
                if (Input::GetKey(KeyCode::S)) movement_direction += GetEntity()->GetBackward();
                if (Input::GetKey(KeyCode::D)) movement_direction += GetEntity()->GetRight();
                if (Input::GetKey(KeyCode::A)) movement_direction += GetEntity()->GetLeft();
                if (Input::GetKey(KeyCode::Q)) movement_direction += GetEntity()->GetDown();
                if (Input::GetKey(KeyCode::E)) movement_direction += GetEntity()->GetUp();
            }
            else if (Input::IsGamepadConnected())
            {
                movement_direction += GetEntity()->GetForward() * -Input::GetGamepadThumbStickLeft().y;
                movement_direction += GetEntity()->GetRight()   * Input::GetGamepadThumbStickLeft().x;
                movement_direction += GetEntity()->GetDown()    * Input::GetGamepadTriggerLeft();
                movement_direction += GetEntity()->GetUp()      * Input::GetGamepadTriggerRight();
            }

            // when in game mode and controlling a physics based camera ignore the pitch
            // this is so the view direction (forward) is never pointing towards the ground or sky
            // cause movement to come a stop
            if (m_physics_body_to_control && Engine::IsFlagSet(EngineMode::Game))
            {
                movement_direction.y = 0.0f;
            }

            movement_direction.Normalize();
        }

        // wheel delta (used to adjust movement speed)
        {
            // accumulate
            m_movement_scroll_accumulator += Input::GetMouseWheelDelta().y * 0.1f;

            // Clamp
            float min = -movement_acceleration + 0.1f; // prevent it from negating or zeroing the acceleration, see translation calculation
            float max =  movement_acceleration * 2.0f; // an empirically chosen max
            m_movement_scroll_accumulator = Helper::Clamp(m_movement_scroll_accumulator, min, max);
        }

        // translation
        {
            Vector3 translation = (movement_acceleration + m_movement_scroll_accumulator) * movement_direction;

            // on shift, double the translation
            if (Input::GetKey(KeyCode::Shift_Left))
            {
                translation *= 3.0f;
            }

            // accelerate
            m_movement_speed += translation * delta_time;

            // apply drag (the clamp is there because at big delta times (movement can become zero or negative/opposite)
            m_movement_speed *= clamp(1.0f - movement_drag * delta_time, 0.1f, numeric_limits<float>::max());

            // clamp it
            if (m_movement_speed.Length() > movement_speed_max)
            {
                m_movement_speed = m_movement_speed.Normalized() * movement_speed_max;
            }

            // translate for as long as there is speed
            if (m_movement_speed != Vector3::Zero)
            {
                if (m_physics_body_to_control)
                {
                    if (Engine::IsFlagSet(EngineMode::Game))
                    {
                        const bool is_grounded   = m_physics_body_to_control->RayTraceIsGrounded();
                        const bool is_underwater = GetEntity()->GetPosition().y <= 0.0f;

                        // walk
                        if (is_grounded)
                        {
                            Vector3 velocity_current = m_physics_body_to_control->GetLinearVelocity();
                            Vector3 velocity_new     = Vector3(m_movement_speed.x * 50.0f, velocity_current.y, m_movement_speed.z * 50.0f);
                            m_physics_body_to_control->SetLinearVelocity(velocity_new);
                        }

                        // swim
                        if (is_underwater)
                        {
                            // buoyancy
                            {
                                float water_density  = 1.03f;
                                float object_density = 0.8f;
                                float total_volume   = m_physics_body_to_control->GetCapsuleVolume();

                                // calculate the submerged portion
                                float submerged_height   = -GetEntity()->GetPosition().y;
                                float total_height       = 1.8f;
                                float submerged_fraction = std::min(std::max(submerged_height / total_height, 0.0f), 1.0f);

                                // compute the displacement volume based on the submerged fraction
                                float displacement_volume = total_volume * submerged_fraction * (object_density / water_density);
                                Vector3 buoyancy_force    = -(water_density * m_physics_body_to_control->GetGravity() * displacement_volume);

                                // compute drag factor
                                float drag_coefficient  = 0.34f;
                                float frontal_area      = std::pow(Helper::PI * m_physics_body_to_control->GetCapsuleRadius(), 2.0f);
                                float linear_velocity_y = water_density * m_physics_body_to_control->GetLinearVelocity().y;
                                float drag_force_y      = 0.5f * water_density * linear_velocity_y * linear_velocity_y * drag_coefficient;

                                // making drag force opposite to the velocity direction
                                if (linear_velocity_y > 0)
                                {
                                    drag_force_y = -drag_force_y;
                                }

                                m_physics_body_to_control->ApplyForce(buoyancy_force * 2500.0f, PhysicsForce::Constant);
                                m_physics_body_to_control->ApplyForce(Vector3(0.0f, drag_force_y, 0.0f) * 200.0f, PhysicsForce::Constant);
                            }

                            // movement
                            Vector3 velocity_current = m_physics_body_to_control->GetLinearVelocity();
                            Vector3 velocity_new     = Vector3(m_movement_speed.x * 20.0f, velocity_current.y, m_movement_speed.z * 20.0f);
                            m_physics_body_to_control->SetLinearVelocity(velocity_new);
                        }
                        
                        // jump
                        if (Input::GetKeyDown(KeyCode::Space) || Input::GetKeyDown(KeyCode::Button_A))
                        {
                            if (is_grounded)
                            {
                                m_physics_body_to_control->ApplyForce(Vector3::Up * 400.0f, PhysicsForce::Impulse);
                            }
                        }
                    }
                    else
                    {
                        m_physics_body_to_control->GetEntity()->Translate(m_movement_speed);
                    }
                }
                else
                {
                    GetEntity()->Translate(m_movement_speed);
                }
            }
        }
    }

    void Camera::ProcessInputLerpToEntity()
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
            const float lerp_duration = 2.0f + Helper::Clamp(m_lerp_to_target_distance * 0.01f, 0.0f, 2.0f);

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
                const Quaternion interpolated_rotation = Quaternion::Lerp(GetEntity()->GetRotation(), m_lerp_to_target_rotation, Helper::Clamp(m_lerp_to_target_alpha, 0.0f, 1.0f));
                GetEntity()->SetRotation(interpolated_rotation);
            }

            // if the lerp has completed or the user has initiated fps control, stop lerping
            if (m_lerp_to_target_alpha >= 1.0f || m_is_controlled_by_keyboard_mouse)
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
        if (shared_ptr<Entity> entity = Renderer::GetCamera()->GetSelectedEntity())
        {
            SP_LOG_INFO("Focusing on entity \"%s\"...", entity->GetObjectName().c_str());

            m_lerp_to_target_position = entity->GetPosition();
            const Vector3 target_direction = (m_lerp_to_target_position - GetEntity()->GetPosition()).Normalized();

            // if the entity has a renderable component, we can get a more accurate target position
            // ...otherwise we apply a simple offset so that the rotation vector doesn't suffer
            if (shared_ptr<Renderable> renderable = entity->GetComponent<Renderable>())
            {
                BoundingBoxType bounding_box_type = renderable->HasInstancing() ? BoundingBoxType::TransformedInstances : BoundingBoxType::Transformed;
                m_lerp_to_target_position -= target_direction * renderable->GetBoundingBox(bounding_box_type).GetExtents().Length() * 2.0f;
            }
            else
            {
                m_lerp_to_target_position -= target_direction;
            }
            SP_ASSERT(!isnan(m_lerp_to_target_distance));

            m_lerp_to_target_rotation = Quaternion::FromLookRotation(entity->GetPosition() - m_lerp_to_target_position).Normalized();
            m_lerp_to_target_distance = Vector3::Distance(m_lerp_to_target_position, GetEntity()->GetPosition());

            const float lerp_angle = acosf(Quaternion::Dot(m_lerp_to_target_rotation.Normalized(), GetEntity()->GetRotation().Normalized())) * Helper::RAD_TO_DEG;

            m_lerp_to_target_p = m_lerp_to_target_distance > 0.1f ? true : false;
            m_lerp_to_target_r = lerp_angle > 1.0f ? true : false;
        }
    }

    void Camera::SetPhysicsBodyToControl(PhysicsBody* physics_body)
    {
        m_physics_body_to_control = physics_body;
    }

    bool Camera::IsWalking()
    {
        if (!m_physics_body_to_control || !m_physics_body_to_control->RayTraceIsGrounded())
            return false;

        return m_physics_body_to_control->GetLinearVelocity().LengthSquared() > 0.001f;
    }

    Matrix Camera::ComputeViewMatrix() const
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
            return Matrix::CreatePerspectiveFieldOfViewLH(GetFovVerticalRad(), Renderer::GetViewport().GetAspectRatio(), near_plane, far_plane);
        }
        else if (m_projection_type == Projection_Orthographic)
        {
            return Matrix::CreateOrthographicLH(Renderer::GetViewport().width, Renderer::GetViewport().height, near_plane, far_plane);
        }

        return Matrix::Identity;
    }
}
