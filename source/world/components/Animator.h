/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==================
#include "Component.h"
#include "../../animation/AnimationEvaluate.h"
#include "../../math/Matrix.h"
#include "../../math/Quaternion.h"
#include "../../math/Vector3.h"
#include "../../rhi/RHI_Vertex.h"
#include <string>
#include <vector>
//=============================

namespace spartan
{
    class Mesh;
    class Render;
    struct Skeleton;
    struct AnimationClip;

    // limbs a script can pin to a target, a hand on a steering wheel or a foot on a pedal
    enum class IkLimb : uint8_t
    {
        HandLeft,
        HandRight,
        FootLeft,
        FootRight,
        Count
    };

    class Animator : public Component
    {
    public:
        Animator(Entity* entity);
        ~Animator();

        static void BeginSkinningBatch();
        static void FlushSkinningBatch();

        void Initialize() override;
        void Tick() override;
        void Remove() override;
        void Stop() override;

        bool Play(const std::string& clip_name);
        void Pause();
        void Resume();
        bool IsPlaying() const { return m_playing; }

        float GetSpeed() const { return m_speed; }
        void SetSpeed(float speed) { m_speed = speed; }
        // the playing clip's clock, a script that sets it every frame with the speed at zero drives the clip by hand
        float GetTime() const { return m_time; }
        void SetTime(float seconds);
        float GetClipDuration(const std::string& name);

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

        // limb ik: the hand or foot goes to a position and takes a rotation given in this entity's space, the
        // space the skeleton lives in, so a character parented to a moving car reaches for things on it without
        // a frame of lag. the rotation is the end joint's own. a target holds until it is set again, weight 0
        // hands the limb back to the clip, and without a pole the elbow or knee keeps the clip's bend plane
        void SetLimbIk(IkLimb limb, const math::Vector3& position, const math::Quaternion& rotation, float weight);
        void SetLimbIkPole(IkLimb limb, const math::Vector3& pole);
        void ClearLimbIk();
        float GetLimbIkWeight(IkLimb limb) const { return m_limb_ik[static_cast<size_t>(limb)].weight; }

        // a joint of the pose last evaluated, in this entity's space, the bind pose before anything played
        math::Vector3 GetJointPosition(const std::string& name);
        math::Quaternion GetJointRotation(const std::string& name);

        // ragdoll / external drivers push local matrices and keep skinning alive
        const Skeleton* GetSkeleton() const;
        const std::vector<math::Matrix>& GetCurrentLocalPose() const { return m_last_local_matrices; }
        void SetExternalPose(const std::vector<math::Matrix>& local_pose);
        void ClearExternalPose();
        bool IsExternalPoseActive() const { return m_external_pose_active; }

        // restore bind joint entities and mesh skin, used when leaving play or ragdoll
        void ApplyBindPose();

        // bone entities mirror the skeleton into the scene graph so the editor and attachment
        // code can see it, skinning itself runs off matrices, so crowds turn this off and save
        // both the entities and the per frame transform writes
        void SetBoneEntitiesEnabled(bool enabled);
        bool GetBoneEntitiesEnabled() const { return m_bone_entities_enabled; }

    private:
        struct FootIkLeg
        {
            int32_t thigh = -1;
            int32_t calf  = -1;
            int32_t foot  = -1;
            int32_t ball  = -1;
            // bind-pose knee bend in model space, pole when the animated leg is too straight to tell
            math::Vector3 knee_pole_bind = math::Vector3(0.0f, 0.0f, -1.0f);
            // where the kneecap faces, in thigh space, so it follows the clip's hip rotation
            math::Vector3 kneecap_thigh_local = math::Vector3(0.0f, 0.0f, -1.0f);
            bool has_kneecap = false;
            // ankle height above the sole with the foot flat, the clip lifting past it is a swing
            float ankle_height = 0.11f;
            // the ik only adds the height difference between the clip floor and the real ground under
            // the foot, on flat ground that is zero and the clip plays untouched, so heel strike, toe
            // off and stride all keep their authored timing
            float smooth_lift = 0.0f;
            // ground under the sole, smoothed in world space so the root climbing a step never drags a
            // planted foot with it
            float smooth_ground_y_world = 0.0f;
            // 0..1, how planted the clip has this foot, read off its height not off a ray
            float smooth_plant = 0.0f;
            // world space, converted to model space per frame so turning never tilts the sole
            math::Vector3 smooth_normal = math::Vector3::Up;
            // clip foot position plus the smoothed lift, sampled before the pelvis moves
            math::Vector3 target_model = math::Vector3::Zero;
            math::Vector3 normal_model = math::Vector3::Up;
            float ground_y_world = 0.0f;
            bool ground_hit = false;
            bool has_smooth = false;
        };
        struct LimbIk
        {
            int32_t upper = -1;
            int32_t lower = -1;
            int32_t end   = -1;
            math::Vector3 position    = math::Vector3::Zero;
            math::Quaternion rotation = math::Quaternion::Identity;
            math::Vector3 pole        = math::Vector3::Zero;
            float weight  = 0.0f;
            bool has_pole = false;
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
        void PublishPoseBounds(Mesh* mesh);
        const std::vector<Render*>& ResolveSkinRenders(Mesh* mesh);
        bool IsSkinDue(Mesh* mesh);
        void ApplyHierarchy(
            const Skeleton& skeleton,
            const std::vector<math::Matrix>& local_matrices
        );
        void SkinFromLocalPose(
            Mesh* mesh,
            const Skeleton& skeleton,
            const std::vector<math::Matrix>& local_matrices,
            bool sample_clip = false
        );
        void ResolveFootIkJoints(const Skeleton& skeleton);
        void ApplyFootIk(
            const Skeleton& skeleton,
            std::vector<math::Matrix>& local_matrices
        );
        void UpdateFootIkLegTarget(
            const std::vector<math::Matrix>& globals,
            FootIkLeg& leg,
            const math::Matrix& model_to_world,
            const math::Matrix& world_to_model,
            Entity* ignore_entity
        );
        bool SolveFootIkLeg(
            const Skeleton& skeleton,
            std::vector<math::Matrix>& local_matrices,
            const std::vector<math::Matrix>& globals,
            FootIkLeg& leg
        );
        void ResolveLimbIkJoints(const Skeleton& skeleton);
        void ApplyLimbIk(
            const Skeleton& skeleton,
            std::vector<math::Matrix>& local_matrices
        );
        bool ResolveJointGlobal(const std::string& name, math::Matrix& out_global);
        int32_t FindJointIndex(const Skeleton& skeleton, const std::string& name) const;
        Mesh* ResolveMesh();
        Entity* FindJointEntity(Entity* parent, const std::string& name) const;
        Entity* FindJointByName(const Skeleton& skeleton, const std::string& name) const;
        bool AttachHand(Entity* hand, Entity* arm, HandAttach& out_attach);

        Mesh* m_mesh = nullptr;
        std::vector<RHI_Vertex_PosTexNorTan> m_bind_vertices;
        std::vector<RHI_Vertex_PosTexNorTan> m_skinned_vertices;
        std::vector<math::Matrix> m_global_matrices;
        std::vector<math::Matrix> m_skin_matrices;
        std::vector<Entity*> m_joint_entities;
        std::vector<BindEntityPose> m_bind_entity_poses;
        HandAttach m_hand_l;
        HandAttach m_hand_r;
        std::string m_current_clip;
        int32_t m_clip_index = -1;
        float m_time              = 0.0f;
        float m_blend_duration    = 0.2f;
        float m_speed             = 1.0f;
        bool m_playing            = false;
        bool m_loop               = true;
        bool m_bind_captured = false;
        bool m_bone_entities_enabled = true;
        bool m_joints_resolved = false;
        bool m_joints_resolve_attempted = false;
        bool m_hands_attached = false;
        bool m_hands_attach_attempted = false;
        bool m_dynamic_blas_ready = false;
        // renders drawing the skinned mesh, resolved once per mesh and checked by id before use
        std::vector<uint64_t> m_skin_render_ids;
        std::vector<Render*> m_skin_renders;
        const Mesh* m_skin_renders_mesh = nullptr;
        bool m_skin_published = false;
        bool m_foot_ik_enabled = false;
        bool m_foot_ik_resolved = false;
        float m_foot_ik_weight = 1.0f;
        // eased master weight, toggling ik must not pop the feet on takeoff and landing
        float m_foot_ik_blend = 0.0f;
        float m_foot_ik_ground_offset = 0.0f;
        float m_foot_ik_pelvis_offset = 0.0f;
        // last root position, the pelvis offset absorbs root height changes so the body stays continuous
        math::Vector3 m_foot_ik_root_previous = math::Vector3::Zero;
        bool m_foot_ik_root_previous_valid = false;
        float m_foot_ik_support_ground_y = 0.0f;
        bool m_foot_ik_has_support = false;
        FootIkLeg m_foot_ik_l;
        FootIkLeg m_foot_ik_r;
        LimbIk m_limb_ik[static_cast<size_t>(IkLimb::Count)];
        bool m_limb_ik_resolved = false;
        std::vector<math::Matrix> m_last_local_matrices;
        // animation layer only, sampled and inertialized but before foot ik. transitions blend this
        // layer, ik is a post process with its own smoothing and re-applies every frame, so folding
        // it into the transition source would count it twice
        std::vector<math::Matrix> m_anim_pose;
        std::vector<math::Matrix> m_anim_pose_previous;
        std::vector<math::Matrix> m_external_local_matrices;
        // per frame scratch, kept alive so the tick and the ik never touch the heap
        std::vector<math::Matrix> m_tick_locals;
        std::vector<math::Matrix> m_ik_globals;
        animation_evaluate::PoseInertializer m_inertializer;
        bool m_external_pose_active = false;
    };
}
