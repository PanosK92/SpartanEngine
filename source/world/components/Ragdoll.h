/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "Component.h"
#include "../../math/BoundingBox.h"
#include "../../math/Matrix.h"
#include "../../math/Quaternion.h"
#include "../../math/Vector3.h"
#include <cstdint>
#include <vector>

namespace physx
{
    class PxRigidDynamic;
    class PxMaterial;
    class PxJoint;
}

namespace sol
{
    class state_view;
}

namespace spartan
{
    class Animator;
    struct Skeleton;

    class Ragdoll : public Component
    {
    public:
        enum class State : uint8_t
        {
            Alive,
            Simulating,
            Frozen
        };

        Ragdoll(Entity* entity);
        ~Ragdoll() override;

        void Initialize() override;
        void Remove() override;
        void Start() override;
        void Stop() override;
        void PreTick() override;
        void Tick() override;

        // after scripts/pedestrians move, snap hit capsule and draw debug
        void LateTick();

        static void RegisterForScripting(sol::state_view state);
        sol::reference AsLua(sol::state_view state) override;

        void Activate(
            const math::Vector3& hit_position,
            const math::Vector3& hit_velocity
        );

        // wake a frozen corpse, returns true if bodies are dynamic and pickable
        bool Wake(
            const math::Vector3& hit_position,
            const math::Vector3& hit_velocity
        );

        // kill residual kick velocity before mouse grab
        void PrepareForPick();

        State GetState() const { return m_state; }
        bool IsDead() const { return m_state != State::Alive; }
        bool IsFrozen() const { return m_state == State::Frozen; }

        void SetHitBodyEnabled(bool enabled);
        bool IsHitBodyEnabled() const { return m_hit_body_wanted; }

    private:
        struct BoneBody
        {
            physx::PxRigidDynamic* actor = nullptr;
            int32_t joint_index = -1;
            // joint_world = actor_to_joint * actor_world (row-vector)
            math::Matrix actor_to_joint = math::Matrix::Identity;
        };

        struct BoneJoint
        {
            physx::PxJoint* joint = nullptr;
            int32_t parent_body = -1;
            int32_t child_body = -1;
            float swing_y = 0.5f;
            float swing_z = 0.5f;
            float twist = 0.3f;
        };

        void CreateHitBody();
        void DestroyHitBody();
        void DestroyRagdoll();
        void DestroyJoints();
        void RecreateJoints();
        void ResetToAlive();
        void SyncHitBody() const;
        bool ComputeHitBodyWorld(
            math::Vector3& out_center,
            math::Quaternion& out_rotation,
            float& out_radius,
            float& out_half_height
        ) const;
        void ProcessHits();
        bool BuildRagdoll(
            const Skeleton& skeleton,
            const std::vector<math::Matrix>& local_matrices,
            const math::Vector3& hit_velocity,
            const math::Vector3& hit_position
        );
        void SyncPoseFromActors();
        void UpdateCullBounds();
        void ApplyCullBounds(const math::BoundingBox& world_box);
        void Freeze();
        bool AreBodiesSleeping() const;
        bool IsFiniteMatrix(const math::Matrix& matrix) const;
        int32_t FindJointIndex(const Skeleton& skeleton, const char* name) const;
        int32_t FindJointIndexAny(const Skeleton& skeleton, std::initializer_list<const char*> names) const;
        // radius from skinned verts weighted to this bone segment, model space
        float EstimateBoneRadius(
            const Skeleton& skeleton,
            const std::vector<math::Matrix>& model_globals,
            int32_t joint_index,
            int32_t child_joint_index,
            float fallback_radius
        ) const;
        int32_t AddBoneBody(
            const std::vector<math::Matrix>& world_globals,
            int32_t joint_index,
            int32_t child_joint_index,
            float radius,
            float mass,
            float angular_damping = 0.35f,
            float max_angular_speed = 25.0f,
            float min_length = 0.08f,
            float max_length = 0.55f
        );
        // ankle to toe, one capsule along the foot
        int32_t AddFootBody(
            const std::vector<math::Matrix>& world_globals,
            int32_t foot_index,
            int32_t ball_index,
            float mass
        );
        void AddBoneJoint(int32_t parent_body, int32_t child_body, float swing_y, float swing_z, float twist);
        void DrawDebug() const;

        Animator* m_animator = nullptr;
        mutable const Skeleton* m_hit_skeleton = nullptr;
        mutable std::vector<math::Matrix> m_hit_globals;
        mutable int32_t m_hit_hips = -1;
        mutable int32_t m_hit_head = -1;
        mutable int32_t m_hit_foot_l = -1;
        mutable int32_t m_hit_foot_r = -1;
        physx::PxRigidDynamic* m_hit_body = nullptr;
        physx::PxMaterial* m_material = nullptr;
        std::vector<BoneBody> m_bodies;
        std::vector<BoneJoint> m_joints;
        std::vector<math::Matrix> m_pose_locals;

        math::Matrix m_entity_world_at_activate = math::Matrix::Identity;
        math::BoundingBox m_cull_bounds_world = math::BoundingBox::Unit;
        bool m_cull_bounds_valid = false;
        State m_state = State::Alive;
        float m_sleep_timer = 0.0f;
        bool m_hit_body_wanted = false;
        math::Vector3 m_hit_center_offset = math::Vector3(0.0f, 0.83f, 0.0f);
    };
}
