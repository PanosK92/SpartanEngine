/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "CameraController.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Physics.h"
#include "../world/components/Light.h"
#include "../world/components/Render.h"
#include "../input/Input.h"
#include "../car/Car.h"
#include "../rendering/Renderer.h"
#include "../core/Window.h"
#include "../display/Display.h"
#include "../xr/Xr.h"
using namespace std;
using namespace spartan::math;
namespace spartan
{
    namespace
    {
        unordered_map<uint64_t, unique_ptr<CameraController>> controllers;
        Physics* fly_controller(Entity* camera_entity)
        {
            if (!camera_entity)
            {
                return nullptr;
            }

            Entity* parent = camera_entity->GetParent();
            if (!parent)
            {
                return nullptr;
            }

            Physics* physics = parent->GetComponent<Physics>();
            if (!physics || physics->GetBodyType() != BodyType::Controller)
            {
                return nullptr;
            }

            return physics;
        }

        // fly control moves the parent capsule, lerp used to move only the camera child, so wasd
        // snapped the eye back onto the capsule at the old location
        void set_camera_world_pose(Entity* camera_entity, const Vector3& position, const Quaternion& rotation)
        {
            if (Physics* physics = fly_controller(camera_entity))
            {
                Entity* parent    = camera_entity->GetParent();
                const Vector3 eye = physics->GetControllerTopLocal();
                parent->SetPosition(position - parent->GetRotation() * eye);
                camera_entity->SetPositionLocal(eye);
                camera_entity->SetRotation(rotation);
                return;
            }

            camera_entity->SetPosition(position);
            camera_entity->SetRotation(rotation);
        }
    }

    CameraController::CameraController(Camera& camera) : m_camera(camera), m_camera_id(camera.GetObjectId()) {}

    CameraController& CameraController::Get(Camera& camera)
    {
        auto& controller = controllers[camera.GetEntity()->GetObjectId()];
        if (!controller || controller->m_camera_id != camera.GetObjectId()) controller = make_unique<CameraController>(camera);
        return *controller;
    }

    void CameraController::Initialize()
    {
        SP_SUBSCRIBE_TO_EVENT(EventType::EntityRemoving, [](const sp_variant& data) {
            controllers.erase(static_cast<Entity*>(get<void*>(data))->GetObjectId());
        });
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldUnloading, [](const sp_variant&) { controllers.clear(); });
    }

    void CameraController::Tick()
    {
        if (Camera* camera = World::GetCamera())
        {
            Get(*camera).ProcessInput();
            camera->RefreshMatrices();
        }
    }

    void CameraController::ResetFpsMotion()
    {
        m_movement_speed        = Vector3::Zero;
        m_jump_velocity         = 0.0f;
        m_jump_time             = 0.0f;
        m_lerp_to_target_p      = false;
        m_lerp_to_target_r      = false;
        m_anim_spring_offset    = Vector3::Zero;
        m_anim_spring_velocity  = Vector3::Zero;
        m_anim_offset_previous  = Vector3::Zero;
        m_anim_rotation_previous = Quaternion::Identity;
        m_gait_phase            = 0.0f;
        m_gait_speed            = 0.0f;
        m_breath_phase          = 0.0f;
        m_fall_speed            = 0.0f;
        m_strafe_speed          = 0.0f;
        m_was_grounded          = true;
    }

    void CameraController::ProcessInput()
    {
        // only the camera the renderer is using responds to input, otherwise every camera in the world would move at once
        if (World::GetCamera() != &m_camera)
        {
            return;
        }

        // car views can parent this camera to the player, body mesh, or vehicle physics.
        // ownership, rather than the current parent's physics state, gates fps input.
        if (Car::IsCameraControlled(m_camera.GetEntity()))
        {
            ResetFpsMotion();
            m_camera.SetFlag(CameraFlags::IsControlled, false);
            if (m_camera.GetFlag(CameraFlags::WantsCursorHidden))
            {
                Input::SetMousePosition(m_mouse_last_position);
                if (!Window::IsFullScreen())
                {
                    Input::SetMouseCursorVisible(true);
                }
                m_camera.SetFlag(CameraFlags::WantsCursorHidden, false);
            }
            return;
        }

        // lerp first so wasd can cancel it and fly control takes over from the current pose this frame
        Input_LerpToEntity();

        if (m_camera.GetFlag(CameraFlags::CanBeControlled) && !m_lerp_to_target_p && !m_lerp_to_target_r)
        {
            Input_FpsControl();
        }
    }

    void CameraController::Input_FpsControl()
    {
        // parameters
        static const float jump_height       = 2.0f;  // target height in meters
        static const float jump_acceleration = 20.0f; // acceleration to reach height in m/s^2
        static const float fly_speed         = 12.0f; // editor fly speed in m/s
        static const float walk_speed        = 2.2f;  // grounded walk speed in m/s
        static const float run_speed         = 5.5f;  // grounded sprint speed in m/s
        float delta_time                     = static_cast<float>(Timer::GetDeltaTimeSec());

        // input mapping
        bool button_move_forward    = Input::GetKey(KeyCode::W);
        bool button_move_backward   = Input::GetKey(KeyCode::S);
        bool button_move_right      = Input::GetKey(KeyCode::D);
        bool button_move_left       = Input::GetKey(KeyCode::A);
        bool button_move_up         = Input::GetKey(KeyCode::E);
        bool button_move_down       = Input::GetKey(KeyCode::Q);
        bool button_sprint          = Input::GetKey(KeyCode::Shift_Left) || Input::GetKey(KeyCode::Left_Shoulder);
        bool button_jump            = Input::GetKeyDown(KeyCode::Space) || Input::GetKeyDown(KeyCode::Button_South);
        bool button_crouch          = Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Button_East); // Left Ctrl or O button
        bool button_flashlight      = Input::GetKeyDown(KeyCode::F) || Input::GetKeyDown(KeyCode::Button_North);
        bool mouse_click_right_down = Input::GetKeyDown(KeyCode::Click_Right);
        bool mouse_click_right      = Input::GetKey(KeyCode::Click_Right);
        bool mouse_click_left_down  = Input::GetKeyDown(KeyCode::Click_Left);
        bool is_playing            = Engine::IsFlagSet(EngineMode::Playing);

        // if the camera is parented to an entity with a physics body, we will control that instead
        Physics* physics_body = nullptr;
        if (Entity* parent = m_camera.GetEntity()->GetParent())
        {
            if (Physics* physics = parent->GetComponent<Physics>())
            {
                physics_body = physics;
            }
        }

        auto update_flashlight = [&]()
        {
            if (!m_flashlight && !is_playing && !m_camera.GetFlag(CameraFlags::Flashlight) && !button_flashlight)
            {
                return;
            }

            // create flashlight entity once
            if (!m_flashlight)
            {
                // entity
                m_flashlight = World::CreateEntity();
                m_flashlight->SetObjectName("flashlight");
                m_flashlight->SetTransient(true); // don't serialize - dynamically created
                m_flashlight->SetParent(m_camera.GetEntity());
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
                m_camera.SetFlag(CameraFlags::Flashlight, !m_camera.GetFlag(CameraFlags::Flashlight));
            }

            // ensure flashlight follows camera and respects active state
            if (m_flashlight)
            {
                // ensure parent is set (in case camera entity was recreated)
                if (m_flashlight->GetParent() != m_camera.GetEntity())
                {
                    m_flashlight->SetParent(m_camera.GetEntity());
                    m_flashlight->SetRotationLocal(Quaternion::Identity);
                }

                // keep the entity active so the world keeps tracking the light,
                // then use intensity to control whether it contributes
                bool flashlight_enabled = m_camera.GetFlag(CameraFlags::Flashlight);
                m_flashlight->SetActive(true);
                
                if (Light* light = m_flashlight->GetComponent<Light>())
                {
                    // set intensity to 0 when off, restore to 2000 when on
                    light->SetIntensity(flashlight_enabled ? 2000.0f : 0.0f);
                }
            }
        };

        // skip fps control if the physics body is disabled (e.g. when in a vehicle)
        if (physics_body && !physics_body->IsEnabled())
        {
            // keep non-movement camera features working while the controller is disabled
            update_flashlight();
            return;
        }

        // remove the previous head animation before look input changes its rotation basis.
        // removing it after pitch input would bake part of the temporary lean into the view.
        m_camera.GetEntity()->SetPositionLocal(m_camera.GetEntity()->GetPositionLocal() - m_anim_offset_previous);
        m_camera.GetEntity()->SetRotationLocal((m_camera.GetEntity()->GetRotationLocal() * m_anim_rotation_previous.Inverse()).Normalized());
        m_anim_offset_previous   = Vector3::Zero;
        m_anim_rotation_previous = Quaternion::Identity;

        // deduce all states into booleans (some states exists as part of the class, so no need to deduce here)
        bool mouse_in_viewport    = Input::GetMouseIsInViewport();
        bool is_controlled        = m_camera.GetFlag(CameraFlags::IsControlled);
        bool wants_cursor_hidden  = m_camera.GetFlag(CameraFlags::WantsCursorHidden);
        bool is_gamepad_connected = Input::IsGamepadConnected();
        bool has_physics_body     = physics_body != nullptr;
        bool is_grounded          = has_physics_body ? physics_body->IsGrounded() : false;
        bool is_crouching         = button_crouch && is_grounded;
        m_is_walking              = (button_move_forward || button_move_backward || button_move_left || button_move_right) && is_grounded;

        // when transitioning from editor to play mode, snap the camera back to the
        // controller's eye height so any free-fly offset accumulated in editor is reset
        if (is_playing && !m_was_playing && has_physics_body && physics_body->GetBodyType() == BodyType::Controller)
        {
            m_camera.GetEntity()->SetPositionLocal(physics_body->GetControllerTopLocal());
        }

        // reset the body animation state on mode changes so no offset leaks into the new mode
        if (is_playing != m_was_playing)
        {
            m_anim_spring_offset     = Vector3::Zero;
            m_anim_spring_velocity   = Vector3::Zero;
            m_anim_offset_previous   = Vector3::Zero;
            m_anim_rotation_previous = Quaternion::Identity;
            m_gait_phase             = 0.0f;
            m_gait_speed             = 0.0f;
            m_fall_speed             = 0.0f;
            m_strafe_speed           = 0.0f;
        }
        m_was_playing = is_playing;

        // behavior: control activation and cursor handling
        {
            bool control_initiated  = mouse_click_right_down && mouse_in_viewport;
            bool control_maintained = mouse_click_right && is_controlled;
            bool is_controlled_new  = control_initiated || control_maintained;
            m_camera.SetFlag(CameraFlags::IsControlled, is_controlled_new);
            is_controlled = is_controlled_new;
    
            if (is_controlled_new && !wants_cursor_hidden)
            {
                m_mouse_last_position = Input::GetMousePosition();
                if (!Window::IsFullScreen())
                {
                    Input::SetMouseCursorVisible(false);
                }
                m_camera.SetFlag(CameraFlags::WantsCursorHidden, true);
            }
            else if (!is_controlled_new && wants_cursor_hidden)
            {
                Input::SetMousePosition(m_mouse_last_position);
                if (!Window::IsFullScreen())
                {
                    Input::SetMouseCursorVisible(true);
                }
                m_camera.SetFlag(CameraFlags::WantsCursorHidden, false);
            }
        }
    
        // behavior: mouse look and movement direction calculation
        Vector3 movement_direction = Vector3::Zero;
        bool is_xr_active = Xr::IsSessionRunning();
        if (is_controlled || is_gamepad_connected)
        {
            // cursor edge wrapping (skip in xr mode - head tracking handles rotation)
            if (is_controlled && !is_xr_active)
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
    
            // mouse and gamepad look - skip in xr mode since head tracking handles rotation
            if (!is_xr_active)
            {
                Quaternion current_rotation = m_camera.GetEntity()->GetRotationLocal();
                Vector2 input_delta = Vector2::Zero;
                if (is_controlled)
                {
                    input_delta = Input::GetMouseDelta() * m_camera.GetMouseSensitivity();
                }
                if (is_gamepad_connected)
                {
                    // gamepad stick is a rate (rotation speed), not accumulated movement like mouse
                    // scale by delta_time and a base rotation speed for framerate-independent behavior
                    const float gamepad_rotation_speed = 120.0f; // degrees per second at full stick deflection
                    input_delta += Input::GetGamepadThumbStickRight() * gamepad_rotation_speed * delta_time;
                }
                Quaternion yaw_increment   = Quaternion::FromAxisAngle(Vector3::Up, input_delta.x * deg_to_rad);
                Quaternion pitch_increment = Quaternion::FromAxisAngle(Vector3::Right, input_delta.y * deg_to_rad);
                Quaternion new_rotation    = yaw_increment * current_rotation * pitch_increment;
                Vector3 current_forward    = current_rotation * Vector3::Forward;
                Vector3 forward            = new_rotation * Vector3::Forward;
                float current_pitch_angle  = asin(-current_forward.y) * rad_to_deg;
                float pitch_angle          = asin(-forward.y) * rad_to_deg;
                bool exceeds_pitch_limit   = pitch_angle > 80.0f || pitch_angle < -80.0f;
                bool recovers_pitch        = abs(pitch_angle) < abs(current_pitch_angle);
                if (exceeds_pitch_limit && !recovers_pitch)
                {
                    new_rotation = yaw_increment * current_rotation;
                }
                m_camera.GetEntity()->SetRotationLocal(new_rotation.Normalized());
            }
    
            // Keyboard and gamepad movement direction
            if (is_controlled)
            {
                if (button_move_forward)
                {
                    movement_direction += m_camera.GetEntity()->GetForward();
                }
                if (button_move_backward)
                {
                    movement_direction += m_camera.GetEntity()->GetBackward();
                }
                if (button_move_right)
                {
                    movement_direction += m_camera.GetEntity()->GetRight();
                }
                if (button_move_left)
                {
                    movement_direction += m_camera.GetEntity()->GetLeft();
                }
                if (button_move_up)
                {
                    movement_direction += Vector3::Up;
                }
                if (button_move_down)
                {
                    movement_direction += Vector3::Down;
                }
            }
            if (is_gamepad_connected)
            {
                movement_direction += m_camera.GetEntity()->GetBackward() * Input::GetGamepadThumbStickLeft().y;
                movement_direction += m_camera.GetEntity()->GetRight()    * Input::GetGamepadThumbStickLeft().x;
                movement_direction += Vector3::Up                * Input::GetGamepadTriggerRight();
                movement_direction += Vector3::Down              * Input::GetGamepadTriggerLeft();
            }
    
            if (has_physics_body && is_playing)
            {
                movement_direction.y = 0.0f;
            }
            movement_direction.Normalize();
        }
    
        // behavior: velocity model, accelerate toward a target velocity and glide to a stop, framerate independent
        {
            m_movement_scroll_accumulator +=
                Input::GetMouseWheelDelta().y *
                0.25f;
            m_movement_scroll_accumulator = clamp(
                m_movement_scroll_accumulator,
                -4.0f,
                6.0f
            );

            bool is_on_foot = is_playing && has_physics_body;
            float fly_speed_scale = powf(
                2.0f,
                m_movement_scroll_accumulator
            );
            float target_speed = is_on_foot ?
                (button_sprint ? run_speed : walk_speed) :
                fly_speed *
                fly_speed_scale *
                (button_sprint ? 10.0f : 1.0f);

            // on foot the body responds fast, the editor fly is snappy on input and releases into a short glide
            bool has_input   = movement_direction.LengthSquared() > 0.0f;
            float accel_rate = is_on_foot ?
                (has_input ? 12.0f : 14.0f) :
                (has_input ? 20.0f : 30.0f);
            m_movement_speed = Vector3::Lerp(m_movement_speed, movement_direction * target_speed, 1.0f - exp(-accel_rate * delta_time));
            if (!has_input && m_movement_speed.LengthSquared() < 0.0001f)
            {
                m_movement_speed = Vector3::Zero;
            }
        }
    
        // behavior: physical body animation, the head is a damped spring excited by gait impacts instead of a plain sine wave
        if (m_camera.GetFlag(CameraFlags::PhysicalBodyAnimation) && is_playing && has_physics_body)
        {
            const float step_frequency   = 1.8f;    // steps per second at 1.4 m/s, cadence scales with the square root of speed like real gait
            const float bob_vertical     = 0.014f;  // vertical travel in meters
            const float bob_lateral      = 0.009f;  // lateral sway in meters
            const float spring_stiffness = 250.0f;  // spring rate of the neck and torso
            const float spring_damping   = 24.0f;   // slightly under critical so impacts settle with a small organic overshoot
            const float step_impact      = 0.10f;   // downward velocity injected at each heel strike in m/s
            const float land_impact      = 0.06f;   // fraction of fall speed turned into a landing dip
            const float breath_frequency = 0.25f;   // breaths per second when idle
            const float breath_amplitude = 0.0025f; // vertical breathing travel in meters

            Vector3 velocity   = physics_body->GetLinearVelocity();
            float planar_speed = Vector3(velocity.x, 0.0f, velocity.z).Length();
            m_gait_speed       = math::lerp(m_gait_speed, is_grounded ? planar_speed : 0.0f, 1.0f - exp(-10.0f * delta_time));

            // track fall speed while airborne and turn it into a dip on touchdown
            if (!is_grounded)
            {
                m_fall_speed = min(m_fall_speed, velocity.y);
            }
            else if (!m_was_grounded)
            {
                m_anim_spring_velocity.y += m_fall_speed * land_impact;
                m_fall_speed              = 0.0f;
            }
            m_was_grounded = is_grounded;

            // spring rest target, gait sway while walking, breathing when idle
            Vector3 spring_target = Vector3::Zero;
            if (m_gait_speed > 0.2f)
            {
                float cadence        = step_frequency * sqrt(m_gait_speed / 1.4f);
                float phase_previous = m_gait_phase;
                m_gait_phase        += pi * cadence * delta_time; // one step per pi, one full stride per two pi

                // heel strike, each step injects a downward impulse that the spring recovers from
                if (static_cast<int>(m_gait_phase / pi) > static_cast<int>(phase_previous / pi))
                {
                    m_anim_spring_velocity.y -= step_impact * (0.4f + 0.6f * min(m_gait_speed / run_speed, 1.0f));
                }
                if (m_gait_phase >= pi_2)
                {
                    m_gait_phase -= pi_2;
                }

                // vertical rises twice per stride, sway shifts weight once per stride, together they trace the figure eight of real head motion
                float amplitude_scale = min(m_gait_speed / walk_speed, 1.5f);
                spring_target.y       = sin(m_gait_phase * 2.0f) * bob_vertical * amplitude_scale;
                spring_target.x       = sin(m_gait_phase) * bob_lateral * amplitude_scale;
                m_breath_phase        = 0.0f;
            }
            else
            {
                m_breath_phase  += pi_2 * breath_frequency * delta_time;
                spring_target.y  = sin(m_breath_phase) * breath_amplitude;
            }

            // damped spring integration, clamped dt keeps it stable across frame hitches
            float anim_dt           = min(delta_time, 0.033f);
            Vector3 spring_accel    = (spring_target - m_anim_spring_offset) * spring_stiffness - m_anim_spring_velocity * spring_damping;
            m_anim_spring_velocity += spring_accel * anim_dt;
            m_anim_spring_offset   += m_anim_spring_velocity * anim_dt;

            // apply this frame's animation to the unanimated view pose
            Vector3 right          = m_camera.GetEntity()->GetRotationLocal() * Vector3::Right;
            Vector3 offset         = right * m_anim_spring_offset.x + Vector3::Up * m_anim_spring_offset.y;
            m_anim_offset_previous = offset;
            m_camera.GetEntity()->SetPositionLocal(m_camera.GetEntity()->GetPositionLocal() + offset);

            // subtle roll from weight shift and strafe lean, subtle pitch nod from vertical motion
            float strafe_speed_target = Vector3::Dot(velocity, right);
            m_strafe_speed            = math::lerp(
                m_strafe_speed,
                strafe_speed_target,
                1.0f - exp(-10.0f * delta_time)
            );

            float roll               = -m_anim_spring_offset.x * 1.2f - m_strafe_speed * 0.01f;
            float pitch              = -m_anim_spring_velocity.y * 0.03f;
            Quaternion anim_rotation = Quaternion::FromAxisAngle(Vector3::Forward, roll) * Quaternion::FromAxisAngle(Vector3::Right, pitch);
            m_camera.GetEntity()->SetRotationLocal((m_camera.GetEntity()->GetRotationLocal() * anim_rotation).Normalized());
            m_anim_rotation_previous = anim_rotation;
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
        
        // behavior: apply movement (skip during focus lerp - the lerp controls the camera)
        bool is_focus_lerping = m_lerp_to_target_p || m_lerp_to_target_r;
        if (!is_focus_lerping && (m_movement_speed != Vector3::Zero || (has_physics_body && is_playing && is_grounded)))
        {
            if (has_physics_body && is_playing)
            {
                if (physics_body->GetBodyType() == BodyType::Controller)
                {
                    physics_body->Move(m_movement_speed * delta_time);
                }
                else if (is_grounded)
                {
                    Vector3 velocity        = physics_body->GetLinearVelocity();
                    Vector3 target_velocity = Vector3(m_movement_speed.x, velocity.y, m_movement_speed.z);
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
                physics_body->Move(m_movement_speed * delta_time);

                // keep the camera at eye height on the controller capsule so flying in
                // editor mode doesn't let the local offset drift from the proper position
                if (physics_body->GetBodyType() == BodyType::Controller)
                {
                    m_camera.GetEntity()->SetPositionLocal(physics_body->GetControllerTopLocal());
                }
            }
            else
            {
                m_camera.GetEntity()->Translate(m_movement_speed * delta_time);
            }
        }

        // behavior: flashlight
        update_flashlight();

        // behaviour: shoot (physics boxes for now)
        if (mouse_click_left_down && mouse_click_right && mouse_in_viewport && is_playing)
        {
            // create entity and name it
            Entity* entity = World::CreateEntity();
            entity->SetObjectName("physics_box");

            // position it in front of the camera
            math::Vector3 spawn_offset = m_camera.GetEntity()->GetForward() * 2.0f; // 2 meters ahead
            entity->SetPosition(m_camera.GetEntity()->GetPosition() + spawn_offset);

            // give it a mesh and a material
            Render* render = entity->AddComponent<Render>();
            render->SetMesh(MeshType::Cube);
            render->SetDefaultMaterial();

            // the default material projects its texture in world space, override to object space so the texture sticks to the cube as it moves
            render->GetMaterialOverrideMutable().uv_world_space = 0.0f;

            // add physics
            Physics* physics = entity->AddComponent<Physics>();
            physics->SetBodyType(BodyType::Box);
            physics->SetStatic(false);
            physics->SetKinematic(false);

            // apply bullet-like impulse
            float bullet_speed = 50.0f; // m/s
            physics->ApplyForce(m_camera.GetEntity()->GetForward() * bullet_speed, PhysicsForce::Impulse);
        }
    }

    void CameraController::Input_LerpToEntity()
    {
        const bool focus_requested = Input::GetKeyDown(KeyCode::F);

        if (!m_lerp_to_target_p && !m_lerp_to_target_r)
        {
            return;
        }

        // only real fly input cancels, mouse delta and left click fire constantly in the editor
        const bool user_cancelled =
            Input::GetKey(KeyCode::W) ||
            Input::GetKey(KeyCode::A) ||
            Input::GetKey(KeyCode::S) ||
            Input::GetKey(KeyCode::D) ||
            Input::GetKey(KeyCode::Q) ||
            Input::GetKey(KeyCode::E) ||
            Input::GetKey(KeyCode::Click_Right) ||
            Input::GetMouseWheelDelta() != Vector2::Zero;

        // f itself must not cancel the lerp it just started
        if (!focus_requested && user_cancelled)
        {
            set_camera_world_pose(m_camera.GetEntity(), m_camera.GetEntity()->GetPosition(), m_camera.GetEntity()->GetRotation());
            m_lerp_to_target_p = false;
            m_lerp_to_target_r = false;
            m_movement_speed   = Vector3::Zero;
            return;
        }

        // 0.25s nearby, up to 0.55s across a large island
        const float lerp_duration = 0.25f + clamp(m_lerp_to_target_distance * 0.00015f, 0.0f, 0.3f);

        m_lerp_to_target_alpha += static_cast<float>(Timer::GetDeltaTimeSec()) / lerp_duration;
        float alpha = clamp(m_lerp_to_target_alpha, 0.0f, 1.0f);
        alpha = alpha * alpha * (3.0f - 2.0f * alpha);

        Vector3 interpolated_position    = m_lerp_from_position;
        Quaternion interpolated_rotation = m_lerp_from_rotation;
        if (m_lerp_to_target_p)
        {
            interpolated_position = Vector3::Lerp(m_lerp_from_position, m_lerp_to_target_position, alpha);
        }
        if (m_lerp_to_target_r)
        {
            interpolated_rotation = Quaternion::Lerp(m_lerp_from_rotation, m_lerp_to_target_rotation, alpha);
        }

        set_camera_world_pose(m_camera.GetEntity(), interpolated_position, interpolated_rotation);

        if (m_lerp_to_target_alpha >= 1.0f)
        {
            m_lerp_to_target_p = false;
            m_lerp_to_target_r = false;
        }
    }

    void CameraController::Focus(Entity* entity, int instance)
    {
        if (Engine::IsFlagSet(EngineMode::Playing))
        {
            return;
        }

        if (!entity)
        {
            return;
        }

        SP_LOG_INFO("Focusing on entity \"%s\"...", entity->GetObjectName().c_str());

        const Vector3 camera_position = m_camera.GetEntity()->GetPosition();
        Vector3 focus_point           = entity->GetPosition();
        BoundingBox focus_box         = BoundingBox::Zero;
        bool has_box                  = false;

        if (Render* render = entity->GetComponent<Render>())
        {
            // the whole box of an instanced renderable is the tile it scatters over, framing that
            // flies away from the prop that was clicked, so one instance frames on its own
            const int selected_instance = instance;
            const bool one     = render->HasInstancing() &&
                                 selected_instance >= 0 &&
                                 static_cast<uint32_t>(selected_instance) < render->GetInstanceCount();

            focus_box = one
                ? render->GetBoundingBoxMesh() * render->GetInstance(static_cast<uint32_t>(selected_instance), true)
                : render->GetBoundingBox();

            focus_point = focus_box.GetCenter();
            has_box     = true;
        }

        Vector3 to_focus = focus_point - camera_position;
        if (to_focus.LengthSquared() < 0.0001f)
        {
            to_focus = m_camera.GetEntity()->GetForward();
        }
        to_focus.Normalize();

        float pullback = 1.0f;
        if (has_box)
        {
            pullback = max(focus_box.GetExtents().Length() * 2.0f, 1.0f);
        }

        m_lerp_to_target_position = focus_point - to_focus * pullback;
        m_lerp_from_position      = camera_position;
        m_lerp_from_rotation      = m_camera.GetEntity()->GetRotation();
        m_lerp_to_target_alpha    = 0.0f;
        m_movement_speed          = Vector3::Zero;
        m_lerp_to_target_rotation = Quaternion::FromLookRotation(focus_point - m_lerp_to_target_position).Normalized();
        m_lerp_to_target_distance = Vector3::Distance(m_lerp_to_target_position, m_lerp_from_position);

        const float dot        = clamp(Quaternion::Dot(m_lerp_to_target_rotation.Normalized(), m_lerp_from_rotation.Normalized()), -1.0f, 1.0f);
        const float lerp_angle = acosf(dot) * rad_to_deg;

        m_lerp_to_target_p = m_lerp_to_target_distance > 0.1f;
        m_lerp_to_target_r = lerp_angle > 1.0f;
    }

}
