/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "../world/components/Camera.h"
#include "../math/Quaternion.h"
namespace spartan
{
    class CameraController
    {
    public:
        explicit CameraController(Camera& camera);
        static CameraController& Get(Camera& camera);
        static void Initialize();
        static void Tick();
        void ResetFpsMotion();
        void Focus(Entity* entity, int instance = -1);
    private:
        void ProcessInput();
        void Input_FpsControl();
        void Input_LerpToEntity();
        Camera& m_camera;
        uint64_t m_camera_id;
        math::Vector2 m_mouse_last_position          = math::Vector2::Zero;
        math::Vector3 m_movement_speed               = math::Vector3::Zero;
        float m_movement_scroll_accumulator          = 0.0f;
        bool m_lerp_to_target_p                      = false;
        bool m_lerp_to_target_r                      = false;
        bool m_is_walking                            = false;
        float m_jump_velocity                        = 0.0f;
        float m_lerp_to_target_alpha                 = 0.0f;
        float m_lerp_to_target_distance              = 0.0f;
        float m_jump_time                            = 0.0f;
        math::Vector3 m_lerp_to_target_position      = math::Vector3::Zero;
        math::Quaternion m_lerp_to_target_rotation   = math::Quaternion::Identity;
        math::Vector3 m_lerp_from_position           = math::Vector3::Zero;
        math::Quaternion m_lerp_from_rotation        = math::Quaternion::Identity;
        bool m_was_playing                           = false;
        Entity* m_flashlight                         = nullptr;

        // physical body animation state
        float m_gait_phase                           = 0.0f;
        float m_gait_speed                           = 0.0f;
        float m_breath_phase                         = 0.0f;
        float m_fall_speed                           = 0.0f;
        float m_strafe_speed                         = 0.0f;
        bool m_was_grounded                          = true;
        math::Vector3 m_anim_spring_offset           = math::Vector3::Zero;
        math::Vector3 m_anim_spring_velocity         = math::Vector3::Zero;
        math::Vector3 m_anim_offset_previous         = math::Vector3::Zero;
        math::Quaternion m_anim_rotation_previous    = math::Quaternion::Identity;
    };
}
