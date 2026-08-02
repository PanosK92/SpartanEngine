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

#include "Component.h"
#include "../../Math/Matrix.h"
#include "../../Math/Quaternion.h"
#include "../../Math/Vector3.h"
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
        void Tick() override;

        static void RegisterForScripting(sol::state_view state);
        sol::reference AsLua(sol::state_view state) override;

        void Activate(
            const math::Vector3& hit_position,
            const math::Vector3& hit_velocity
        );

        State GetState() const { return m_state; }
        bool IsDead() const { return m_state != State::Alive; }

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
        };

        void CreateHitBody();
        void DestroyHitBody();
        void DestroyRagdoll();
        void SyncHitBody() const;
        void ProcessHits();
        bool BuildRagdoll(
            const Skeleton& skeleton,
            const std::vector<math::Matrix>& local_matrices,
            const math::Vector3& hit_velocity
        );
        void SyncPoseFromActors();
        void UpdateCullBounds();
        void Freeze();
        bool AreBodiesSleeping() const;
        bool IsFiniteMatrix(const math::Matrix& matrix) const;
        int32_t FindJointIndex(const Skeleton& skeleton, const char* name) const;
        int32_t FindJointIndexAny(const Skeleton& skeleton, std::initializer_list<const char*> names) const;
        int32_t AddBoneBody(
            const std::vector<math::Matrix>& world_globals,
            int32_t joint_index,
            int32_t child_joint_index,
            float radius,
            float mass
        );
        void AddBoneJoint(int32_t parent_body, int32_t child_body, float swing_y, float swing_z, float twist);

        Animator* m_animator = nullptr;
        physx::PxRigidDynamic* m_hit_body = nullptr;
        physx::PxMaterial* m_material = nullptr;
        std::vector<BoneBody> m_bodies;
        std::vector<BoneJoint> m_joints;
        std::vector<math::Matrix> m_pose_locals;
        void LogMatrix(const char* label, const math::Matrix& matrix) const;
        void LogSyncSample(const char* reason);

        math::Matrix m_entity_world_at_activate = math::Matrix::Identity;
        State m_state = State::Alive;
        float m_sleep_timer = 0.0f;
        bool m_hit_body_wanted = false;
        math::Vector3 m_hit_center_offset = math::Vector3(0.0f, 0.83f, 0.0f);
        uint32_t m_debug_sync_count = 0;
        bool m_debug_logged_bad = false;
    };
}
