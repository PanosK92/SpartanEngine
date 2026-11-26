/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===========================
#include "World.h"
#include "Entity.h"
#include "components/Animator.h"
#include "components/Camera.h"
#include "components/Physics.h"
#include "components/Render.h"
#include "../car/Car.h"
#include "../car/CarPhysics.h"
#include "../car/CarSimulation.h"
#include "../core/Engine.h"
#include "../file_system/FileSystem.h"
#include "../geometry/Mesh.h"
#include "../math/Helper.h"
#include "../math/Quaternion.h"
#include "../math/Vector3.h"
#include "../physics/PhysicsWorld.h"
#include "../resource/ResourceCache.h"
#include "../scripting/Scripting.h"
#include <sol/sol.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
//======================================

namespace spartan::player_body
{
    namespace detail
    {
        constexpr const char* model_path = "project/models/human_male/human_male.gltf";

        // the model, in its own space: y up, he faces -z as an imported gltf does
        constexpr float eye_height      = 1.69f; // standing
        constexpr float eye_up          = 0.092f; // the eye from the head joint
        constexpr float eye_forward     = 0.092f;
        constexpr float stand_back      = 0.10f; // how far behind the camera his eyes stay on foot, so a look down sees a chest and not inside it

        // ground speed baked into the locomotion clips, playback is scaled by camera speed over clip speed so the feet do not slide
        constexpr float walk_clip_speed = 1.25f;
        constexpr float run_clip_speed  = 6.3f;
        constexpr float idle_below      = 0.25f; // m/s
        constexpr float run_above       = 3.4f;

        // the hand: from the wrist joint to the middle of the palm, and from the palm's skin up to the joint
        constexpr float palm_along      = 0.078f;
        constexpr float palm_depth      = 0.018f;
        // the foot: from the ankle joint forward to the ball, and from the sole up to the joint
        constexpr float ball_along      = 0.12f;
        constexpr float sole_depth      = 0.09f;

        constexpr float hand_travel     = 80.0f; // degrees of wheel a hand follows, past that the rim slides through it
        constexpr float reach_time      = 0.35f; // seconds for hands and feet to settle on the controls

        struct BindFrame
        {
            math::Quaternion rotation = math::Quaternion::Identity;
            math::Quaternion frame    = math::Quaternion::Identity;
        };

        struct ColumnFrame
        {
            math::Vector3 position = math::Vector3::Zero;
            math::Vector3 axis     = math::Vector3::Forward;
        };

        struct State
        {
            std::shared_ptr<Mesh> mesh;
            Entity* root                  = nullptr;
            Animator* animator            = nullptr;
            std::vector<Entity*> head_entities;

            math::Vector3 pelvis          = math::Vector3::Zero;
            BindFrame bind_hand_left;
            BindFrame bind_hand_right;
            BindFrame bind_foot_left;
            BindFrame bind_foot_right;

            bool started                  = false;
            bool load_failed              = false;
            int visible                   = -1;
            int head_visible              = -1;
            std::string clip;
            std::string mode;
            float speed                   = 0.0f;
            float reach                   = 0.0f;
            float wheel_angle             = 0.0f;
            float brake                   = 0.0f;
            float clutch                  = 0.0f;
            bool has_last_camera          = false;
            math::Vector3 last_camera     = math::Vector3::Zero;
            bool has_facing               = false;
            math::Vector3 facing          = math::Vector3::Forward;
            bool has_stand_height         = false;
            float stand_height            = eye_height;
            bool has_lean                 = false;
            float lean                    = 0.0f;
            std::unordered_map<std::string, math::Vector3> last_hands;
        };

        inline State state;

        inline float smoothstep(float x)
        {
            x = std::clamp(x, 0.0f, 1.0f);
            return x * x * (3.0f - 2.0f * x);
        }

        inline bool is_head_part_name(const std::string& name)
        {
            return name == "head" || name == "hair" || name == "eyes";
        }

        inline bool flat_direction(const math::Vector3& v, math::Vector3& out)
        {
            const float length = std::sqrt(v.x * v.x + v.z * v.z);
            if (length < 0.0001f)
            {
                return false;
            }
            out = math::Vector3(v.x / length, 0.0f, v.z / length);
            return true;
        }

        inline float axis_turn_sign()
        {
            static const float sign = (math::Quaternion::FromAxisAngle(math::Vector3::Up, math::pi_div_2) * math::Vector3::Forward).x > 0.0f ? 1.0f : -1.0f;
            return sign;
        }

        inline const math::Quaternion& turn_around()
        {
            static const math::Quaternion q = math::Quaternion::FromAxisAngle(math::Vector3::Up, math::pi);
            return q;
        }

        inline BindFrame make_bind_frame(Animator* animator, const char* joint, const math::Vector3& along, const math::Vector3& back)
        {
            BindFrame frame;
            frame.rotation = animator->GetJointRotation(joint);
            frame.frame    = math::Quaternion::FromLookRotation(along, back);
            return frame;
        }

        inline void play_clip(const char* name, bool loop)
        {
            if (!state.animator || state.clip == name)
            {
                return;
            }
            state.animator->SetLoop(loop);
            if (state.animator->Play(name))
            {
                state.clip = name;
            }
        }

        inline void show_head(bool visible)
        {
            const int desired = visible ? 1 : 0;
            if (state.head_visible == desired)
            {
                return;
            }
            state.head_visible = desired;
            for (Entity* entity : state.head_entities)
            {
                if (entity)
                {
                    entity->SetActive(visible);
                }
            }
        }

        inline void show_body(bool visible)
        {
            const int desired = visible ? 1 : 0;
            if (state.visible != desired)
            {
                state.visible = desired;
                if (state.root)
                {
                    state.root->SetActive(visible);
                }
            }
        }

        inline Car* get_occupied_car()
        {
            for (Car* car : Car::GetAll())
            {
                if (car && car->IsOccupied())
                {
                    return car;
                }
            }
            return nullptr;
        }

        inline bool is_player_camera(Camera* camera)
        {
            if (!camera || !camera->GetEntity() || !camera->GetFlag(CameraFlags::PhysicalBodyAnimation))
            {
                return false;
            }
            Entity* parent = camera->GetEntity()->GetParent();
            if (!parent)
            {
                return false;
            }
            Physics* physics = parent->GetComponent<Physics>();
            return physics && physics->GetBodyType() == BodyType::Controller;
        }

        inline bool read_car_driver_motion(uint64_t car_id, float& out_seated)
        {
            sol::state_view lua = scripting::get_state();
            if (!lua.lua_state())
            {
                return false;
            }
            sol::object object = lua["CarDriver"];
            if (!object.is<sol::table>())
            {
                return false;
            }
            sol::table motion = object.as<sol::table>();
            sol::object car_field = motion["car"];
            uint64_t motion_car_id = 0;
            if (car_field.is<std::string>())
            {
                motion_car_id = std::strtoull(car_field.as<std::string>().c_str(), nullptr, 10);
            }
            else if (car_field.is<uint64_t>())
            {
                motion_car_id = car_field.as<uint64_t>();
            }
            if (motion_car_id != car_id)
            {
                return false;
            }
            sol::object seated_field = motion["seated"];
            if (!seated_field.is<float>())
            {
                return false;
            }
            out_seated = std::clamp(seated_field.as<float>(), 0.0f, 1.0f);
            return true;
        }

        inline bool load()
        {
            if (state.root && World::EntityExists(state.root) && state.animator)
            {
                return true;
            }
            if (state.load_failed)
            {
                return false;
            }

            uint32_t flags = Mesh::GetDefaultFlags();
            flags &= ~static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale);
            flags &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);

            std::shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(model_path, flags);
            Entity* root = mesh ? mesh->GetRootEntity() : nullptr;

            // if a cached mesh lost its root entity across world teardown, reload it fresh once
            if (mesh && (!root || !World::EntityExists(root)))
            {
                ResourceCache::Remove(mesh);
                mesh = ResourceCache::Load<Mesh>(model_path, flags);
                root = mesh ? mesh->GetRootEntity() : nullptr;
            }

            if (!mesh || !root)
            {
                SP_LOG_WARNING("player_body: failed to load %s", model_path);
                state.load_failed = true;
                return false;
            }

            root->SetObjectName("player_body");
            root->SetTransient(true);

            state.head_entities.clear();
            std::vector<Entity*> descendants;
            root->GetDescendants(&descendants);
            for (Entity* entity : descendants)
            {
                if (!entity)
                {
                    continue;
                }
                entity->SetTransient(true);
                Entity* parent = entity->GetParent();
                if (entity->GetComponent<Render>() && (is_head_part_name(entity->GetObjectName()) || (parent && is_head_part_name(parent->GetObjectName()))))
                {
                    state.head_entities.push_back(entity);
                }
            }

            Animator* animator = root->GetComponent<Animator>();
            if (!animator)
            {
                animator = root->AddComponent<Animator>();
            }
            if (!animator)
            {
                state.load_failed = true;
                return false;
            }

            // make sure newly spawned entities are immediately live in the world
            World::ProcessPendingAdditions();

            // ensure rest pose is evaluated before reading bind joints
            animator->ApplyBindPose();

            state.mesh     = mesh;
            state.root     = root;
            state.animator = animator;

            // the rest pose, read before any clip plays: he stands in a t-pose, palms down, toes forward
            state.pelvis          = animator->GetJointPosition("pelvis");
            state.bind_hand_left  = make_bind_frame(animator, "hand.l", (animator->GetJointPosition("middle_01.l") - animator->GetJointPosition("hand.l")).Normalized(), math::Vector3::Up);
            state.bind_hand_right = make_bind_frame(animator, "hand.r", (animator->GetJointPosition("middle_01.r") - animator->GetJointPosition("hand.r")).Normalized(), math::Vector3::Up);
            state.bind_foot_left  = make_bind_frame(animator, "foot.l", math::Vector3(0.0f, 0.0f, -1.0f), math::Vector3::Up);
            state.bind_foot_right = make_bind_frame(animator, "foot.r", math::Vector3(0.0f, 0.0f, -1.0f), math::Vector3::Up);
            return true;
        }

        inline void start_session()
        {
            if (!load())
            {
                return;
            }
            state.started          = true;
            state.visible          = -1;
            state.head_visible     = -1;
            state.clip.clear();
            state.mode.clear();
            state.speed            = 0.0f;
            state.reach            = 0.0f;
            state.wheel_angle      = 0.0f;
            state.brake            = 0.0f;
            state.clutch           = 0.0f;
            state.has_last_camera  = false;
            state.last_hands.clear();
            state.has_stand_height = false;
            state.stand_height     = eye_height;
            state.has_lean         = false;
            state.lean             = 0.0f;

            state.animator->SetBlendDuration(0.2f);
            state.animator->SetSpeed(1.0f);
            show_body(true);
            show_head(false);
            play_clip("idle", true);
        }

        inline void hide_session()
        {
            if (!state.root || !state.animator)
            {
                return;
            }
            if (state.mode != "hidden")
            {
                state.mode = "hidden";
                state.animator->ClearLimbIk();
                state.animator->SetFootIkEnabled(false);
            }
            show_body(false);
        }

        // how high the camera rides over the ground while the player stands. crouching is read directly from the capsule controller
        // height (standing GetControllerTopLocal().y is ~0.77m, crouching drops to ~0.37m), so non-floor static colliders never
        // falsely hide the body
        inline void track_stand_height(Entity* camera_entity, const math::Vector3& position, bool& out_standing, bool& out_grounded)
        {
            Entity* player   = camera_entity->GetParent();
            Physics* physics = player ? player->GetComponent<Physics>() : nullptr;
            out_grounded     = !physics || physics->IsGrounded();

            float controller_eye_height = eye_height;
            out_standing                = true;
            if (physics && physics->GetBodyType() == BodyType::Controller)
            {
                const float top_local_y = physics->GetControllerTopLocal().y;
                out_standing            = top_local_y > 0.60f;
                // capsule center to bottom = top_local_y + eye_offset_from_top (0.13m), so eye to bottom = 2 * top_local_y + 0.13m
                if (out_standing)
                {
                    controller_eye_height = std::clamp(2.0f * top_local_y + 0.13f, 1.4f, 2.0f);
                }
            }

            math::Vector3 hit_position;
            Entity* hit_entity = nullptr;
            const bool hit = out_grounded && out_standing && PhysicsWorld::RaycastStatic(position, math::Vector3::Down, 2.5f, hit_position, hit_entity) && hit_entity != nullptr;
            if (hit)
            {
                const float height = position.y - hit_position.y;
                if (std::fabs(height - controller_eye_height) < 0.25f)
                {
                    if (!state.has_stand_height)
                    {
                        state.stand_height     = height;
                        state.has_stand_height = true;
                    }
                    else
                    {
                        state.stand_height += (height - state.stand_height) * 0.15f;
                    }
                    return;
                }
            }

            if (!state.has_stand_height)
            {
                state.stand_height     = controller_eye_height;
                state.has_stand_height = true;
            }
            else if (out_standing)
            {
                state.stand_height += (controller_eye_height - state.stand_height) * 0.15f;
            }
        }

        inline void tick_on_foot(Entity* camera_entity, float dt)
        {
            Animator* animator            = state.animator;
            const math::Vector3 position  = camera_entity->GetPosition();
            math::Vector3 forward         = state.has_facing ? state.facing : math::Vector3::Forward;
            if (flat_direction(camera_entity->GetForward(), forward))
            {
                state.facing     = forward;
                state.has_facing = true;
            }

            if (state.mode != "on_foot")
            {
                state.mode             = "on_foot";
                state.has_last_camera  = false;
                state.has_stand_height = false;
                state.has_lean         = false;
                animator->ClearLimbIk();
                animator->SetBlendDuration(0.2f);
                animator->SetFootIkEnabled(true);
                animator->SetFootIkWeight(1.0f);
            }

            bool standing = true;
            bool grounded = true;
            track_stand_height(camera_entity, position, standing, grounded);

            // how fast the player travels over the ground picks the clip and its rate
            float speed = 0.0f;
            if (state.has_last_camera && dt > 0.0f)
            {
                const float dx = position.x - state.last_camera.x;
                const float dz = position.z - state.last_camera.z;
                speed          = std::sqrt(dx * dx + dz * dz) / dt;
            }
            state.last_camera     = position;
            state.has_last_camera = true;
            state.speed          += (std::min(speed, 12.0f) - state.speed) * std::clamp(dt * 10.0f, 0.0f, 1.0f);

            animator->SetFootIkEnabled(grounded);
            if (!grounded)
            {
                play_clip("air_jump", false);
                animator->SetSpeed(1.0f);
            }
            else if (state.speed < idle_below)
            {
                play_clip("idle", true);
                animator->SetSpeed(1.0f);
            }
            else if (state.speed < run_above)
            {
                play_clip("walk", true);
                animator->SetSpeed(std::clamp(state.speed / walk_clip_speed, 0.5f, 2.6f));
            }
            else
            {
                play_clip("run", true);
                animator->SetSpeed(std::clamp(state.speed / run_clip_speed, 0.6f, 1.6f));
            }

            // he stands on the ground under the camera, facing where it looks, his eyes a little behind it however far the clip leans him
            const float feet = position.y - state.stand_height + (grounded ? animator->GetFootIkGroundOffset() : 0.0f);
            const float lean = eye_forward - animator->GetJointPosition("head").z;
            if (state.has_lean)
            {
                state.lean += (lean - state.lean) * std::clamp(dt * 12.0f, 0.0f, 1.0f);
            }
            else
            {
                state.lean     = lean;
                state.has_lean = true;
            }
            const float back = state.lean + stand_back;
            state.root->SetRotation(math::Quaternion::FromLookRotation(math::Vector3(-forward.x, 0.0f, -forward.z), math::Vector3::Up));
            state.root->SetPosition(math::Vector3(position.x - forward.x * back, feet, position.z - forward.z * back));

            // there is no crouch for him yet, a crouching player has no body
            show_body(standing);
            show_head(false);
        }

        // a target a car anchor gives a hand or a foot, in the body's own space: where the end joint goes and how it turns
        inline void limb_target(const BindFrame& bind, const math::Vector3& position, const math::Quaternion& rotation, float along, float depth, math::Vector3& out_joint, math::Quaternion& out_rotation)
        {
            const math::Quaternion to_model = state.root->GetRotation().Inverse();
            const math::Vector3 joint       = position - (rotation * math::Vector3::Forward) * along + (rotation * math::Vector3::Up) * depth;
            const math::Quaternion turned   = to_model * rotation;
            out_joint                       = to_model * (joint - state.root->GetPosition());
            out_rotation                    = turned * bind.frame.Inverse() * bind.rotation;
        }

        // the wheel's angle from straight ahead, in degrees and past a full turn, read off the wheel anchor against the column anchor
        inline bool track_wheel(Car* car, ColumnFrame& out_column)
        {
            Entity* wheel  = car->GetDriverAnchor("wheel");
            Entity* column = car->GetDriverAnchor("column");
            if (!wheel || !column)
            {
                state.wheel_angle = 0.0f;
                return false;
            }
            const math::Vector3 axis = column->GetRotation() * math::Vector3::Forward;
            const math::Vector3 zero = column->GetRotation() * math::Vector3::Up;
            const math::Vector3 now  = wheel->GetRotation() * math::Vector3::Up;
            const float angle        = std::atan2(math::Vector3::Dot(math::Vector3::Cross(zero, now), axis), math::Vector3::Dot(zero, now)) * math::rad_to_deg;

            float wrapped = std::fmod(state.wheel_angle, 360.0f);
            if (wrapped < 0.0f)
            {
                wrapped += 360.0f;
            }
            float step = std::fmod(angle - wrapped + 180.0f, 360.0f);
            if (step < 0.0f)
            {
                step += 360.0f;
            }
            step -= 180.0f;
            state.wheel_angle += step;

            out_column.position = column->GetPosition();
            out_column.axis     = axis;
            return true;
        }

        inline void tick_hand(Car* car, const ColumnFrame* column, const char* name, const BindFrame& bind, IkLimb limb, float weight)
        {
            Entity* anchor = car->GetDriverAnchor(name);
            if (!anchor || weight <= 0.0f)
            {
                state.animator->SetLimbIk(limb, math::Vector3::Zero, math::Quaternion::Identity, 0.0f);
                state.last_hands.erase(name);
                return;
            }
            math::Vector3 position    = anchor->GetPosition();
            math::Quaternion rotation = anchor->GetRotation();

            // past its travel the hand stays where it was and the rim slides through it
            const float excess = state.wheel_angle - std::clamp(state.wheel_angle, -hand_travel, hand_travel);
            if (column && excess != 0.0f)
            {
                const math::Quaternion back = math::Quaternion::FromAxisAngle(column->axis, -excess * math::deg_to_rad * axis_turn_sign());
                position = column->position + back * (position - column->position);
                rotation = back * rotation;
            }

            math::Vector3 joint;
            math::Quaternion turned;
            limb_target(bind, position, rotation, palm_along, palm_depth, joint, turned);

            // the pose is solved a frame after it is asked for, so ask for where the rim will be
            auto it = state.last_hands.find(name);
            if (it != state.last_hands.end())
            {
                const math::Vector3 previous = it->second;
                it->second = joint;
                joint      = joint + (joint - previous);
            }
            else
            {
                state.last_hands.emplace(name, joint);
            }
            state.animator->SetLimbIk(limb, joint, turned, weight);
        }

        inline void tick_foot(Car* car, const BindFrame& bind, IkLimb limb, const char* idle_anchor, const char* press_anchor, float pressed, float weight)
        {
            Entity* idle  = car->GetDriverAnchor(idle_anchor);
            Entity* press = car->GetDriverAnchor(press_anchor);
            Entity* from  = idle ? idle : press;
            Entity* to    = press ? press : idle;
            if (!from || weight <= 0.0f)
            {
                state.animator->SetLimbIk(limb, math::Vector3::Zero, math::Quaternion::Identity, 0.0f);
                return;
            }
            const math::Vector3 position    = math::Vector3::Lerp(from->GetPosition(), to->GetPosition(), pressed);
            const math::Quaternion rotation = math::Quaternion::Lerp(from->GetRotation(), to->GetRotation(), pressed);
            math::Vector3 joint;
            math::Quaternion turned;
            limb_target(bind, position, rotation, ball_along, -sole_depth, joint, turned);
            state.animator->SetLimbIk(limb, joint, turned, weight);
        }

        // where the seat anchor puts him: his hip joint on it, facing its forward, his back along its up
        inline void seat_pose(Entity* seat, math::Vector3& out_position, math::Quaternion& out_rotation)
        {
            out_rotation = seat->GetRotation() * turn_around();
            out_position = seat->GetPosition() - out_rotation * state.pelvis;
        }

        // where the camera puts him while it carries his head in or out: his eyes at the camera, his feet below them
        inline void follow_pose(Entity* camera_entity, math::Vector3& out_position, math::Quaternion& out_rotation)
        {
            math::Vector3 forward = state.has_facing ? state.facing : math::Vector3::Forward;
            flat_direction(camera_entity->GetForward(), forward);
            out_rotation = math::Quaternion::FromLookRotation(math::Vector3(-forward.x, 0.0f, -forward.z), math::Vector3::Up);
            const math::Vector3 head = state.animator->GetJointPosition("head");
            const float current_stand = state.has_stand_height ? state.stand_height : eye_height;
            const math::Vector3 eye(head.x, head.y + eye_up + (current_stand - eye_height), head.z - eye_forward);
            out_position = camera_entity->GetPosition() - out_rotation * eye;
        }

        inline void tick_in_car(Car* car, Entity* seat, Entity* camera_entity, float dt)
        {
            Animator* animator = state.animator;
            Entity* car_root   = car->GetRootEntity();
            float seated       = 1.0f;
            const bool has_motion = car_root && read_car_driver_motion(car_root->GetObjectId(), seated);

            if (state.mode != "in_car")
            {
                state.mode        = "in_car";
                state.reach       = 0.0f;
                state.wheel_angle = 0.0f;
                state.last_hands.clear();
                animator->SetFootIkEnabled(false);
                animator->SetBlendDuration(0.25f);
            }

            if (has_motion)
            {
                // the car is moving the camera through the door, the clip is scrubbed along with it
                play_clip("car_enter", false);
                animator->SetSpeed(0.0f);
                animator->SetTime(seated * animator->GetClipDuration("car_enter"));
                state.reach = smoothstep((seated - 0.72f) / 0.28f);
            }
            else
            {
                play_clip("drive", true);
                animator->SetSpeed(1.0f);
                state.reach = std::clamp(state.reach + dt / reach_time, 0.0f, 1.0f);
            }

            math::Vector3 position;
            math::Quaternion rotation;
            seat_pose(seat, position, rotation);
            if (has_motion)
            {
                math::Vector3 follow_position;
                math::Quaternion follow_rotation;
                follow_pose(camera_entity, follow_position, follow_rotation);
                const float settle = smoothstep((seated - 0.40f) / 0.60f);
                position = math::Vector3::Lerp(follow_position, position, settle);
                rotation = math::Quaternion::Lerp(follow_rotation, rotation, settle);
            }
            state.root->SetRotation(rotation);
            state.root->SetPosition(position);

            // hands on the rim, feet on the pedals the driver is using
            const float weight = smoothstep(state.reach);
            ColumnFrame column;
            const bool has_column = track_wheel(car, column);
            Physics* physics = car_root ? car_root->GetComponent<Physics>() : nullptr;
            float brake_target  = 0.0f;
            float clutch_target = 0.0f;
            if (physics)
            {
                CarPhysics& car_physics = CarPhysics::Get(*physics);
                brake_target = car_physics.GetVehicleBrake() > 0.02f ? 1.0f : 0.0f;
                if (::car::Simulation* simulation = car_physics.GetVehicleSimulation())
                {
                    clutch_target = (1.0f - simulation->get_clutch()) > 0.05f ? 1.0f : 0.0f;
                }
            }
            const float ease = std::clamp(dt * 9.0f, 0.0f, 1.0f);
            state.brake  += (brake_target - state.brake) * ease;
            state.clutch += (clutch_target - state.clutch) * ease;

            tick_hand(car, has_column ? &column : nullptr, "hand_left",  state.bind_hand_left,  IkLimb::HandLeft,  weight);
            tick_hand(car, has_column ? &column : nullptr, "hand_right", state.bind_hand_right, IkLimb::HandRight, weight);
            tick_foot(car, state.bind_foot_right, IkLimb::FootRight, "foot_throttle", "foot_brake",  smoothstep(state.brake),  weight);
            tick_foot(car, state.bind_foot_left,  IkLimb::FootLeft,  "foot_rest",     "foot_clutch", smoothstep(state.clutch), weight);

            show_body(true);
            const bool external_camera = World::GetActiveCameraOverride() != nullptr;
            show_head((external_camera || car->GetView() != CarView::Interior) && !has_motion);
        }
    }

    inline void Stop()
    {
        using namespace detail;
        if (state.root && World::EntityExists(state.root) && state.animator)
        {
            state.animator->ClearLimbIk();
            state.animator->SetFootIkEnabled(false);
            state.animator->Stop();
            show_body(false);
        }
        state.started = false;
        state.clip.clear();
        state.mode.clear();
        state.last_hands.clear();
    }

    inline void Clear()
    {
        using namespace detail;
        state = {};
    }

    inline void Tick(float dt)
    {
        using namespace detail;

        if (World::IsLoadingFromFile())
        {
            return;
        }
        if (!Engine::IsFlagSet(EngineMode::Playing))
        {
            if (state.started)
            {
                Stop();
            }
            return;
        }
        if (Engine::IsFlagSet(EngineMode::Paused) && state.started)
        {
            return;
        }

        Camera* camera        = World::GetCamera();
        Entity* camera_entity = camera ? camera->GetEntity() : nullptr;
        if (!camera || !camera_entity)
        {
            hide_session();
            return;
        }

        Car* car     = get_occupied_car();
        Entity* seat = car ? car->GetDriverAnchor("seat") : nullptr;
        const bool on_foot_active = !car && is_player_camera(camera);
        const bool in_car_active  = car != nullptr && seat != nullptr;

        if (!on_foot_active && !in_car_active)
        {
            hide_session();
            return;
        }

        if (!state.started)
        {
            start_session();
        }
        if (!state.root || !state.animator || !World::EntityExists(state.root))
        {
            return;
        }

        if (in_car_active)
        {
            tick_in_car(car, seat, camera_entity, dt);
        }
        else
        {
            tick_on_foot(camera_entity, dt);
        }
    }
}
