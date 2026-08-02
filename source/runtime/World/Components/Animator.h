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

//= INCLUDES ==================
#include "Component.h"
#include "../../Math/Matrix.h"
#include "../../Math/Quaternion.h"
#include "../../Math/Vector3.h"
#include "../../RHI/RHI_Vertex.h"
#include <string>
#include <vector>
//=============================

namespace spartan
{
    class Mesh;
    struct Skeleton;
    struct AnimationClip;

    class Animator : public Component
    {
    public:
        Animator(Entity* entity);
        ~Animator() = default;

        void Initialize() override;
        void Tick() override;
        void Remove() override;

        static void RegisterForScripting(sol::state_view state);
        sol::reference AsLua(sol::state_view state) override;

        bool Play(const std::string& clip_name);
        void Stop();
        bool IsPlaying() const { return m_playing; }

        float GetSpeed() const { return m_speed; }
        void SetSpeed(float speed) { m_speed = speed; }

        bool GetLoop() const { return m_loop; }
        void SetLoop(bool loop) { m_loop = loop; }

        float GetBlendDuration() const { return m_blend_duration; }
        void SetBlendDuration(float seconds)
        {
            m_blend_duration = seconds < 0.0f ? 0.0f : seconds;
        }

        const std::string& GetCurrentClip() const { return m_current_clip; }

        bool GetFootIkEnabled() const { return m_foot_ik_enabled; }
        void SetFootIkEnabled(bool enabled) { m_foot_ik_enabled = enabled; }

        float GetFootIkWeight() const { return m_foot_ik_weight; }
        void SetFootIkWeight(float weight)
        {
            m_foot_ik_weight = weight < 0.0f ? 0.0f : (weight > 1.0f ? 1.0f : weight);
        }

        // raise entity by this so bind-pose soles sit on the ground, any model
        float GetFootIkGroundOffset();

        // world y under the lowest planted foot, root should follow this not a center ray
        bool HasFootIkSupportGround() const { return m_foot_ik_has_support; }
        float GetFootIkSupportGroundY() const { return m_foot_ik_support_ground_y; }

    private:
        struct FootIkLeg
        {
            int32_t thigh = -1;
            int32_t calf  = -1;
            int32_t foot  = -1;
            int32_t ball  = -1;
            // bind-pose local directions, not cardinal axes (feet can sit at 45 deg)
            math::Vector3 sole_up_local = math::Vector3::Up;
            math::Vector3 toe_fwd_local = math::Vector3(0.0f, 0.0f, -1.0f);
            // bind-pose knee bend in model space, keeps ik from flipping backwards
            math::Vector3 knee_pole_bind = math::Vector3(0.0f, 0.0f, -1.0f);
            // bind hip-to-ankle height, restores standing length when planting
            float bind_hip_foot_y = 0.85f;
            float ankle_height = 0.11f;
            math::Vector3 smooth_target = math::Vector3::Zero;
            math::Vector3 smooth_normal = math::Vector3::Up;
            math::Vector3 smooth_forward = math::Vector3(0.0f, 0.0f, -1.0f);
            float smooth_weight = 0.0f;
            float contact_dy = 0.0f;
            float ground_y_world = 0.0f;
            bool ground_hit = false;
            bool use_for_pelvis = false;
            bool contact_active = false;
            bool has_smooth = false;
        };
        struct BindEntityPose
        {
            math::Vector3 position = math::Vector3::Zero;
            math::Quaternion rotation = math::Quaternion::Identity;
            math::Vector3 scale = math::Vector3::One;
        };

        struct HandAttach
        {
            Entity* hand = nullptr;
            Entity* original_parent = nullptr;
            BindEntityPose original_local;
            bool attached = false;
        };

        void CaptureBindPose();
        void ResolveJointEntities(const Skeleton& skeleton);
        void AttachHandsToArms();
        void RestoreHands();
        void RestoreBindEntityPoses();
        void EnsureDynamicBlas(Mesh* mesh);
        void MarkBlasNeedsRefit(Mesh* mesh);
        void ApplyHierarchy(
            const Skeleton& skeleton,
            const std::vector<math::Matrix>& local_matrices
        );
        void ResolveFootIkJoints(const Skeleton& skeleton);
        void ApplyFootIk(
            const Skeleton& skeleton,
            std::vector<math::Matrix>& local_matrices
        );
        void UpdateFootIkLegTarget(
            const Skeleton& skeleton,
            const std::vector<math::Matrix>& local_matrices,
            FootIkLeg& leg,
            const math::Matrix& model_to_world,
            const math::Matrix& world_to_model,
            Entity* ignore_entity
        );
        bool SolveFootIkLeg(
            const Skeleton& skeleton,
            std::vector<math::Matrix>& local_matrices,
            FootIkLeg& leg
        );
        int32_t FindJointIndex(const Skeleton& skeleton, const std::string& name) const;
        Mesh* ResolveMesh();
        Entity* FindJointEntity(Entity* parent, const std::string& name) const;
        Entity* FindJointByName(const Skeleton& skeleton, const std::string& name) const;
        bool AttachHand(Entity* hand, Entity* arm, HandAttach& out_attach);

        Mesh* m_mesh = nullptr;
        std::vector<RHI_Vertex_PosTexNorTan> m_bind_vertices;
        std::vector<RHI_Vertex_PosTexNorTan> m_skinned_vertices;
        std::vector<Entity*> m_joint_entities;
        std::vector<BindEntityPose> m_bind_entity_poses;
        HandAttach m_hand_l;
        HandAttach m_hand_r;
        std::string m_current_clip;
        int32_t m_clip_index = -1;
        int32_t m_prev_clip_index = -1;
        float m_time              = 0.0f;
        float m_prev_time         = 0.0f;
        float m_blend_weight      = 1.0f;
        float m_blend_duration    = 0.2f;
        float m_speed             = 1.0f;
        bool m_playing            = false;
        bool m_loop               = true;
        bool m_blending           = false;
        bool m_bind_captured = false;
        bool m_joints_resolved = false;
        bool m_joints_resolve_attempted = false;
        bool m_hands_attached = false;
        bool m_hands_attach_attempted = false;
        bool m_dynamic_blas_ready = false;
        bool m_foot_ik_enabled = false;
        bool m_foot_ik_resolved = false;
        float m_foot_ik_weight = 1.0f;
        float m_foot_ik_ground_offset = 0.0f;
        float m_foot_ik_pelvis_offset = 0.0f;
        float m_foot_ik_support_ground_y = 0.0f;
        bool m_foot_ik_has_support = false;
        FootIkLeg m_foot_ik_l;
        FootIkLeg m_foot_ik_r;
    };
}
