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

#include "pch.h"
#include "Ragdoll.h"
#include "Animator.h"
#include "Physics.h"
#include "Render.h"
#include "../../Animation/Skeleton.h"
#include "../../Math/BoundingBox.h"
#include "../../Core/Engine.h"
#include "../../Core/Timer.h"
#include "../../Math/Quaternion.h"
#include "../../Physics/PhysicsWorld.h"
#include "../Entity.h"
#include <cctype>
#include <cmath>
#include <cstdio>

SP_WARNINGS_OFF
#ifdef DEBUG
    #define _DEBUG 1
    #undef NDEBUG
#else
    #define NDEBUG 1
    #undef _DEBUG
#endif
#define PX_PHYSX_STATIC_LIB
#include <physx/PxPhysicsAPI.h>
#include <sol/sol.hpp>
SP_WARNINGS_ON

using namespace std;
using namespace spartan::math;
using namespace physx;

namespace spartan
{
    namespace
    {
        constexpr float freeze_sleep_seconds = 1.25f;
        constexpr float max_launch_speed = 16.0f;
        constexpr float hit_speed = 3.5f;
        constexpr float hit_impulse = 40.0f;
        constexpr float hit_radius = 0.28f;
        constexpr float hit_half_height = 0.55f;

        PxTransform to_px(const Vector3& pos, const Quaternion& rot)
        {
            return PxTransform(
                PxVec3(pos.x, pos.y, pos.z),
                PxQuat(rot.x, rot.y, rot.z, rot.w)
            );
        }

        Matrix matrix_from_px(const PxTransform& pose)
        {
            return Matrix(
                Vector3(pose.p.x, pose.p.y, pose.p.z),
                Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w),
                Vector3::One
            );
        }

        void tag_shapes(PxRigidActor* actor, PxU32 collision_type)
        {
            if (!actor)
            {
                return;
            }

            PxShape* shapes[16];
            const PxU32 count = actor->getShapes(shapes, 16);
            for (PxU32 i = 0; i < count; ++i)
            {
                PxFilterData fd = shapes[i]->getSimulationFilterData();
                fd.word2 = collision_type;
                shapes[i]->setSimulationFilterData(fd);
            }
        }

        string to_lower(string value)
        {
            for (char& c : value)
            {
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            }
            return value;
        }

        Vector3 clamp_vector_length(Vector3 value, float max_length)
        {
            const float length_squared = value.LengthSquared();
            if (length_squared > max_length * max_length && length_squared > 0.0001f)
            {
                value *= max_length / sqrtf(length_squared);
            }
            return value;
        }

        Vector3 actor_linear_velocity(Entity* entity)
        {
            if (!entity)
            {
                return Vector3::Zero;
            }

            if (Physics* physics = entity->GetComponent<Physics>())
            {
                return physics->GetLinearVelocity();
            }

            return Vector3::Zero;
        }

        bool is_finite(float value)
        {
            return isfinite(value) != 0;
        }

        float matrix_max_abs(const Matrix& matrix)
        {
            float max_abs = 0.0f;
            max_abs = max(max_abs, fabsf(matrix.m00));
            max_abs = max(max_abs, fabsf(matrix.m01));
            max_abs = max(max_abs, fabsf(matrix.m02));
            max_abs = max(max_abs, fabsf(matrix.m03));
            max_abs = max(max_abs, fabsf(matrix.m10));
            max_abs = max(max_abs, fabsf(matrix.m11));
            max_abs = max(max_abs, fabsf(matrix.m12));
            max_abs = max(max_abs, fabsf(matrix.m13));
            max_abs = max(max_abs, fabsf(matrix.m20));
            max_abs = max(max_abs, fabsf(matrix.m21));
            max_abs = max(max_abs, fabsf(matrix.m22));
            max_abs = max(max_abs, fabsf(matrix.m23));
            max_abs = max(max_abs, fabsf(matrix.m30));
            max_abs = max(max_abs, fabsf(matrix.m31));
            max_abs = max(max_abs, fabsf(matrix.m32));
            max_abs = max(max_abs, fabsf(matrix.m33));
            return max_abs;
        }

        bool matrix_looks_bad(const Matrix& matrix)
        {
            const Vector3 t = matrix.GetTranslation();
            const Vector3 s = matrix.GetScale();
            if (!is_finite(t.x) || !is_finite(t.y) || !is_finite(t.z))
            {
                return true;
            }
            if (!is_finite(s.x) || !is_finite(s.y) || !is_finite(s.z))
            {
                return true;
            }
            if (fabsf(t.x) > 100.0f || fabsf(t.y) > 100.0f || fabsf(t.z) > 100.0f)
            {
                return true;
            }
            if (s.x < 0.1f || s.y < 0.1f || s.z < 0.1f || s.x > 4.0f || s.y > 4.0f || s.z > 4.0f)
            {
                return true;
            }
            if (matrix_max_abs(matrix) > 1000.0f)
            {
                return true;
            }
            return false;
        }
    }

    Ragdoll::Ragdoll(Entity* entity) : Component(entity)
    {
        m_hit_center_offset = Vector3(0.0f, hit_half_height + hit_radius, 0.0f);
        m_hit_body_wanted = false;
    }

    Ragdoll::~Ragdoll()
    {
        Remove();
    }

    void Ragdoll::Initialize()
    {
        m_animator = GetEntity()->GetComponent<Animator>();
        GetEntity()->AddTag("pedestrian");
        if (m_hit_body_wanted)
        {
            CreateHitBody();
        }
    }

    void Ragdoll::ResetToAlive()
    {
        DestroyRagdoll();
        DestroyHitBody();

        Entity* root = GetEntity();
        if (root)
        {
            vector<Entity*> nodes;
            nodes.push_back(root);
            root->GetDescendants(&nodes);
            for (Entity* node : nodes)
            {
                if (node)
                {
                    if (Render* render = node->GetComponent<Render>())
                    {
                        render->ClearBoundingBoxOverride();
                    }
                }
            }
        }

        if (m_animator)
        {
            m_animator->ClearExternalPose();
        }

        m_state = State::Alive;
        m_sleep_timer = 0.0f;
        m_pose_locals.clear();
        m_cull_bounds_valid = false;
        m_entity_world_at_activate = Matrix::Identity;
        m_debug_sync_count = 0;
        m_debug_logged_bad = false;
    }

    void Ragdoll::Start()
    {
        m_animator = GetEntity()->GetComponent<Animator>();
        ResetToAlive();
        if (m_hit_body_wanted)
        {
            CreateHitBody();
        }
    }

    void Ragdoll::Stop()
    {
        ResetToAlive();
    }

    void Ragdoll::Remove()
    {
        ResetToAlive();
        if (m_material)
        {
            m_material->release();
            m_material = nullptr;
        }
    }

    void Ragdoll::LogMatrix(const char* label, const Matrix& matrix) const
    {
        const Vector3 t = matrix.GetTranslation();
        const Vector3 s = matrix.GetScale();
        const Quaternion r = matrix.GetRotation();
        SP_LOG_INFO(
            "ragdoll %s t=(%.3f, %.3f, %.3f) s=(%.3f, %.3f, %.3f) q=(%.3f, %.3f, %.3f, %.3f) max_abs=%.3f bad=%d",
            label ? label : "matrix",
            t.x, t.y, t.z,
            s.x, s.y, s.z,
            r.x, r.y, r.z, r.w,
            matrix_max_abs(matrix),
            matrix_looks_bad(matrix) ? 1 : 0
        );
    }

    void Ragdoll::LogSyncSample(const char* reason)
    {
        const Skeleton* skeleton = m_animator ? m_animator->GetSkeleton() : nullptr;
        SP_LOG_INFO(
            "ragdoll sync_sample reason=%s sync_n=%u bodies=%zu pose_locals=%zu",
            reason ? reason : "?",
            m_debug_sync_count,
            m_bodies.size(),
            m_pose_locals.size()
        );
        LogMatrix("entity_world_locked", m_entity_world_at_activate);

        for (size_t b = 0; b < m_bodies.size(); ++b)
        {
            const BoneBody& body = m_bodies[b];
            if (!body.actor || body.joint_index < 0)
            {
                continue;
            }

            const string joint_name = (skeleton &&
                body.joint_index < static_cast<int32_t>(skeleton->joint_names.size()))
                ? skeleton->joint_names[static_cast<size_t>(body.joint_index)]
                : "?";

            const Matrix actor_world = matrix_from_px(body.actor->getGlobalPose());
            const Matrix joint_world = body.actor_to_joint * actor_world;
            const Matrix joint_model = joint_world * m_entity_world_at_activate.Inverted();

            SP_LOG_INFO(
                "ragdoll body[%zu] joint=%d(%s) parent=%d",
                b,
                body.joint_index,
                joint_name.c_str(),
                (skeleton && body.joint_index < static_cast<int32_t>(skeleton->joint_count))
                    ? skeleton->parent_indices[static_cast<size_t>(body.joint_index)]
                    : -999
            );
            LogMatrix("  actor_world", actor_world);
            LogMatrix("  actor_to_joint", body.actor_to_joint);
            LogMatrix("  joint_world", joint_world);
            LogMatrix("  joint_model", joint_model);

            if (body.joint_index >= 0 &&
                body.joint_index < static_cast<int32_t>(m_pose_locals.size()))
            {
                LogMatrix("  pose_local", m_pose_locals[static_cast<size_t>(body.joint_index)]);
            }
        }
    }

    void Ragdoll::SetHitBodyEnabled(bool enabled)
    {
        m_hit_body_wanted = enabled;
        if (m_state != State::Alive)
        {
            return;
        }

        if (enabled)
        {
            CreateHitBody();
        }
        else
        {
            DestroyHitBody();
        }
    }

    void Ragdoll::PreTick()
    {
        // before any Render::Tick frustum test, physics has already stepped
        if (m_state == State::Simulating)
        {
            UpdateCullBounds();
        }
        else if (m_state == State::Frozen && m_cull_bounds_valid)
        {
            ApplyCullBounds(m_cull_bounds_world);
        }
    }

    void Ragdoll::Tick()
    {
        if (!Engine::IsFlagSet(EngineMode::Playing) || Engine::IsFlagSet(EngineMode::Paused))
        {
            return;
        }

        if (m_state == State::Alive)
        {
            if (m_hit_body_wanted)
            {
                if (!m_hit_body)
                {
                    CreateHitBody();
                }
                SyncHitBody();
                ProcessHits();
            }
            return;
        }

        if (m_state == State::Frozen)
        {
            if (m_cull_bounds_valid)
            {
                ApplyCullBounds(m_cull_bounds_world);
            }
            ProcessHits();
            return;
        }

        if (m_state != State::Simulating)
        {
            return;
        }

        SyncPoseFromActors();

        const float dt = clamp(static_cast<float>(Timer::GetDeltaTimeSec()), 0.0f, 0.1f);
        if (AreBodiesSleeping())
        {
            m_sleep_timer += dt;
            if (m_sleep_timer >= freeze_sleep_seconds)
            {
                Freeze();
            }
        }
        else
        {
            m_sleep_timer = 0.0f;
        }
    }

    void Ragdoll::Activate(const Vector3& hit_position, const Vector3& hit_velocity)
    {
        if (m_state == State::Simulating)
        {
            return;
        }

        if (m_state == State::Frozen)
        {
            Wake(hit_position, hit_velocity);
            return;
        }

        if (!m_animator)
        {
            m_animator = GetEntity()->GetComponent<Animator>();
        }

        if (!m_animator)
        {
            SP_LOG_ERROR("ragdoll activate failed, no animator on %s", GetEntity()->GetObjectName().c_str());
            return;
        }

        const Skeleton* skeleton = m_animator->GetSkeleton();
        if (!skeleton || skeleton->joint_count == 0)
        {
            SP_LOG_ERROR("ragdoll activate failed, no skeleton on %s", GetEntity()->GetObjectName().c_str());
            return;
        }

        vector<Matrix> locals = m_animator->GetCurrentLocalPose();
        SP_LOG_INFO(
            "ragdoll activate entity=%s joints=%u locals=%zu hit=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f)",
            GetEntity()->GetObjectName().c_str(),
            skeleton->joint_count,
            locals.size(),
            hit_position.x, hit_position.y, hit_position.z,
            hit_velocity.x, hit_velocity.y, hit_velocity.z
        );

        string names;
        for (uint32_t i = 0; i < skeleton->joint_count; ++i)
        {
            if (i > 0)
            {
                names += ", ";
            }
            names += "[" + to_string(i) + "]" + skeleton->joint_names[i];
            if (skeleton->parent_indices[i] >= 0)
            {
                names += "(p=" + to_string(skeleton->parent_indices[i]) + ")";
            }
        }
        SP_LOG_INFO("ragdoll skeleton: %s", names.c_str());

        if (locals.size() != skeleton->joint_count)
        {
            SP_LOG_WARNING(
                "ragdoll current locals size %zu != joint_count %u, using bind pose",
                locals.size(),
                skeleton->joint_count
            );
            locals = skeleton->bind_local_matrices;
        }
        if (locals.size() != skeleton->joint_count)
        {
            SP_LOG_ERROR("ragdoll activate failed, bind locals size mismatch");
            return;
        }

        // lock entity transform, all motion comes from joint locals after this
        Entity* entity = GetEntity();
        m_entity_world_at_activate = Matrix(
            entity->GetPosition(),
            entity->GetRotation(),
            entity->GetScale()
        );
        LogMatrix("entity_world_at_activate", m_entity_world_at_activate);

        for (uint32_t i = 0; i < min<uint32_t>(skeleton->joint_count, 8u); ++i)
        {
            char label[64];
            snprintf(label, sizeof(label), "activate_local[%u:%s]", i, skeleton->joint_names[i].c_str());
            LogMatrix(label, locals[i]);
        }

        m_animator->SetFootIkEnabled(false);
        DestroyHitBody();

        Vector3 launch = hit_velocity;
        if (launch.LengthSquared() < 0.01f)
        {
            Vector3 away = entity->GetPosition() - hit_position;
            away.y = 0.0f;
            if (away.LengthSquared() < 0.001f)
            {
                away = entity->GetRotation() * Vector3(0.0f, 0.0f, -1.0f);
            }
            away.Normalize();
            launch = away * 8.0f;
        }

        SP_LOG_INFO("ragdoll launch=(%.2f, %.2f, %.2f)", launch.x, launch.y, launch.z);

        if (!BuildRagdoll(*skeleton, locals, launch))
        {
            SP_LOG_ERROR("ragdoll BuildRagdoll failed for %s", entity->GetObjectName().c_str());
            if (m_hit_body_wanted)
            {
                CreateHitBody();
            }
            return;
        }

        m_pose_locals = locals;
        m_animator->SetExternalPose(m_pose_locals);
        m_state = State::Simulating;
        m_sleep_timer = 0.0f;
        m_debug_sync_count = 0;
        m_debug_logged_bad = false;
        SP_LOG_INFO("ragdoll simulating bodies=%zu joints=%zu", m_bodies.size(), m_joints.size());
        LogSyncSample("activate_done");
        UpdateCullBounds();
    }

    void Ragdoll::CreateHitBody()
    {
        if (m_hit_body || m_state != State::Alive || !m_hit_body_wanted)
        {
            return;
        }

        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        if (!physics)
        {
            return;
        }

        lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());

        if (!m_material)
        {
            m_material = physics->createMaterial(0.55f, 0.45f, 0.05f);
        }

        Entity* entity = GetEntity();
        const Vector3 center = entity->GetPosition() + m_hit_center_offset;

        PxRigidDynamic* actor = physics->createRigidDynamic(to_px(center, entity->GetRotation()));
        if (!actor)
        {
            return;
        }

        PxCapsuleGeometry geometry(hit_radius, hit_half_height);
        PxShape* shape = physics->createShape(geometry, *m_material, true);
        if (!shape)
        {
            actor->release();
            return;
        }

        shape->setLocalPose(PxTransform(PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
        shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
        shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
        actor->attachShape(*shape);
        shape->release();

        actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
        actor->userData = reinterpret_cast<void*>(entity);
        tag_shapes(actor, physics_collision_pedestrian);
        PhysicsWorld::AddActor(actor);
        m_hit_body = actor;
    }

    void Ragdoll::DestroyHitBody()
    {
        if (!m_hit_body)
        {
            return;
        }

        lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
        PhysicsWorld::RemoveActor(m_hit_body);
        m_hit_body->release();
        m_hit_body = nullptr;
    }

    void Ragdoll::DestroyJoints()
    {
        lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());

        for (BoneJoint& joint : m_joints)
        {
            if (joint.joint)
            {
                joint.joint->release();
                joint.joint = nullptr;
            }
        }
    }

    void Ragdoll::RecreateJoints()
    {
        vector<BoneJoint> specs = m_joints;
        m_joints.clear();

        for (const BoneJoint& spec : specs)
        {
            AddBoneJoint(
                spec.parent_body,
                spec.child_body,
                spec.swing_y,
                spec.swing_z,
                spec.twist
            );
        }
    }

    void Ragdoll::DestroyRagdoll()
    {
        DestroyJoints();
        m_joints.clear();

        lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
        for (BoneBody& body : m_bodies)
        {
            if (body.actor)
            {
                PhysicsWorld::RemoveActor(body.actor);
                body.actor->release();
                body.actor = nullptr;
            }
        }
        m_bodies.clear();
    }

    void Ragdoll::SyncHitBody() const
    {
        if (!m_hit_body || !GetEntity())
        {
            return;
        }

        Entity* entity = GetEntity();
        m_hit_body->setKinematicTarget(
            to_px(entity->GetPosition() + m_hit_center_offset, entity->GetRotation())
        );
    }

    void Ragdoll::ProcessHits()
    {
        Entity* self = GetEntity();
        if (!self)
        {
            return;
        }

        const vector<PhysicsContact>& contacts = PhysicsWorld::GetFrameContacts();
        for (const PhysicsContact& contact : contacts)
        {
            Entity* other = nullptr;
            if (contact.entity_a == self)
            {
                other = contact.entity_b;
            }
            else if (contact.entity_b == self)
            {
                other = contact.entity_a;
            }
            else
            {
                continue;
            }

            if (!other || other == self || other->HasTag("pedestrian") || other->GetComponent<Ragdoll>())
            {
                continue;
            }

            // physx normal points from actor1 to actor0
            Vector3 normal = contact.normal;
            if (contact.entity_a != self)
            {
                normal = -normal;
            }

            Vector3 impulse = contact.impulse;
            if (contact.entity_a != self)
            {
                impulse = -impulse;
            }

            // ignore bounced cube velocity pointing back at the camera
            const Vector3 other_to_self = self->GetPosition() - other->GetPosition();
            Vector3 launch = actor_linear_velocity(other);
            if (Vector3::Dot(launch, other_to_self) < 0.0f)
            {
                launch = Vector3::Zero;
            }

            const float impulse_mag = impulse.Length();
            if (launch.LengthSquared() < hit_speed * hit_speed && impulse_mag < hit_impulse)
            {
                continue;
            }

            if (launch.LengthSquared() < hit_speed * hit_speed)
            {
                if (impulse_mag >= hit_impulse)
                {
                    launch = impulse.Normalized() * min(impulse_mag * 0.04f, max_launch_speed);
                }
                else if (normal.LengthSquared() > 0.0001f)
                {
                    launch = normal.Normalized() * hit_speed;
                }
                else if (other_to_self.LengthSquared() > 0.0001f)
                {
                    launch = other_to_self.Normalized() * hit_speed;
                }
                else
                {
                    continue;
                }
            }

            Vector3 hit_position = contact.position;
            if (hit_position.LengthSquared() < 0.0001f)
            {
                hit_position = self->GetPosition() + Vector3(0.0f, 1.0f, 0.0f);
            }

            Activate(hit_position, launch);
            return;
        }
    }

    bool Ragdoll::IsFiniteMatrix(const Matrix& matrix) const
    {
        const Vector3 t = matrix.GetTranslation();
        const Vector3 s = matrix.GetScale();
        if (!is_finite(t.x) || !is_finite(t.y) || !is_finite(t.z))
        {
            return false;
        }
        if (!is_finite(s.x) || !is_finite(s.y) || !is_finite(s.z))
        {
            return false;
        }
        if (fabsf(t.x) > 1.0e4f || fabsf(t.y) > 1.0e4f || fabsf(t.z) > 1.0e4f)
        {
            return false;
        }
        if (s.x < 0.01f || s.y < 0.01f || s.z < 0.01f || s.x > 10.0f || s.y > 10.0f || s.z > 10.0f)
        {
            return false;
        }
        return true;
    }

    int32_t Ragdoll::FindJointIndex(const Skeleton& skeleton, const char* name) const
    {
        if (!name || skeleton.joint_names.size() != skeleton.joint_count)
        {
            return -1;
        }

        // exact match only, substring matches bind the wrong bones and spike the mesh
        const string needle = to_lower(name);
        for (uint32_t i = 0; i < skeleton.joint_count; ++i)
        {
            if (to_lower(skeleton.joint_names[i]) == needle)
            {
                return static_cast<int32_t>(i);
            }
        }

        return -1;
    }

    int32_t Ragdoll::FindJointIndexAny(const Skeleton& skeleton, initializer_list<const char*> names) const
    {
        for (const char* name : names)
        {
            const int32_t index = FindJointIndex(skeleton, name);
            if (index >= 0)
            {
                return index;
            }
        }
        return -1;
    }

    int32_t Ragdoll::AddBoneBody(
        const vector<Matrix>& world_globals,
        int32_t joint_index,
        int32_t child_joint_index,
        float radius,
        float mass
    )
    {
        if (joint_index < 0 || joint_index >= static_cast<int32_t>(world_globals.size()))
        {
            return -1;
        }

        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        if (!physics || !m_material)
        {
            return -1;
        }

        const Matrix& joint_world = world_globals[static_cast<size_t>(joint_index)];
        Vector3 start = joint_world.GetTranslation();
        Vector3 end = start + joint_world.GetRotation() * Vector3(0.0f, 0.18f, 0.0f);
        if (child_joint_index >= 0 && child_joint_index < static_cast<int32_t>(world_globals.size()))
        {
            end = world_globals[static_cast<size_t>(child_joint_index)].GetTranslation();
        }

        Vector3 dir = end - start;
        float length = dir.Length();
        if (length < 0.05f)
        {
            dir = Vector3::Up;
            length = 0.16f;
        }
        else
        {
            dir /= length;
        }

        length = clamp(length, 0.08f, 0.42f);
        const float half_height = max(0.025f, (length * 0.5f) - radius);
        const Vector3 center = start + dir * (length * 0.5f);
        const Quaternion rotation = Quaternion::FromRotation(Vector3::Right, dir);
        const Matrix actor_world(center, rotation, Vector3::One);

        PxRigidDynamic* actor = physics->createRigidDynamic(to_px(center, rotation));
        if (!actor)
        {
            return -1;
        }

        PxCapsuleGeometry geometry(radius, half_height);
        PxShape* shape = physics->createShape(geometry, *m_material, true);
        if (!shape)
        {
            actor->release();
            return -1;
        }

        actor->attachShape(*shape);
        shape->release();

        PxRigidBodyExt::setMassAndUpdateInertia(*actor, mass);
        actor->setSolverIterationCounts(6, 2);
        actor->setMaxLinearVelocity(max_launch_speed);
        actor->setMaxAngularVelocity(14.0f);
        actor->setLinearDamping(0.4f);
        actor->setAngularDamping(0.65f);
        actor->userData = reinterpret_cast<void*>(GetEntity());
        tag_shapes(actor, physics_collision_ragdoll);
        PhysicsWorld::AddActor(actor);

        BoneBody body;
        body.actor = actor;
        body.joint_index = joint_index;
        // joint in actor space, row-vector: joint_world = actor_to_joint * actor_world
        body.actor_to_joint = joint_world * actor_world.Inverted();
        m_bodies.push_back(body);

        SP_LOG_INFO(
            "ragdoll add_body idx=%d child=%d mass=%.1f radius=%.3f half_h=%.3f center=(%.2f, %.2f, %.2f) len=%.2f",
            joint_index,
            child_joint_index,
            mass,
            radius,
            half_height,
            center.x, center.y, center.z,
            length
        );
        LogMatrix("  create_joint_world", joint_world);
        LogMatrix("  create_actor_world", actor_world);
        LogMatrix("  create_actor_to_joint", body.actor_to_joint);

        return static_cast<int32_t>(m_bodies.size() - 1);
    }

    void Ragdoll::AddBoneJoint(
        int32_t parent_body,
        int32_t child_body,
        float swing_y,
        float swing_z,
        float twist
    )
    {
        if (parent_body < 0 || child_body < 0 ||
            parent_body >= static_cast<int32_t>(m_bodies.size()) ||
            child_body >= static_cast<int32_t>(m_bodies.size()))
        {
            return;
        }

        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        PxRigidDynamic* actor_a = m_bodies[static_cast<size_t>(parent_body)].actor;
        PxRigidDynamic* actor_b = m_bodies[static_cast<size_t>(child_body)].actor;
        if (!physics || !actor_a || !actor_b)
        {
            return;
        }

        // anchor at the child joint origin in world space
        const Matrix child_joint_world =
            m_bodies[static_cast<size_t>(child_body)].actor_to_joint *
            matrix_from_px(actor_b->getGlobalPose());
        const Vector3 anchor = child_joint_world.GetTranslation();
        const PxVec3 world_anchor(anchor.x, anchor.y, anchor.z);

        const PxTransform pose_a = actor_a->getGlobalPose();
        const PxTransform pose_b = actor_b->getGlobalPose();
        const PxTransform frame_a = pose_a.transformInv(PxTransform(world_anchor));
        const PxTransform frame_b = pose_b.transformInv(PxTransform(world_anchor));

        PxD6Joint* joint = PxD6JointCreate(*physics, actor_a, frame_a, actor_b, frame_b);
        if (!joint)
        {
            return;
        }

        joint->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
        joint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
        joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
        joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLIMITED);
        joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLIMITED);
        joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLIMITED);
        joint->setTwistLimit(PxJointAngularLimitPair(-twist, twist));
        joint->setSwingLimit(PxJointLimitCone(swing_y, swing_z));
        joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, false);

        BoneJoint entry;
        entry.joint = joint;
        entry.parent_body = parent_body;
        entry.child_body = child_body;
        entry.swing_y = swing_y;
        entry.swing_z = swing_z;
        entry.twist = twist;
        m_joints.push_back(entry);
    }

    bool Ragdoll::BuildRagdoll(
        const Skeleton& skeleton,
        const vector<Matrix>& local_matrices,
        const Vector3& hit_velocity
    )
    {
        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        if (!physics)
        {
            return false;
        }

        lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());

        if (!m_material)
        {
            m_material = physics->createMaterial(0.6f, 0.5f, 0.0f);
        }

        vector<Matrix> model_globals(local_matrices.size());
        skeleton.ComputeGlobalPose(local_matrices, model_globals);

        // engine is row-vector: child = local * parent, world = model * entity
        vector<Matrix> world_globals(model_globals.size());
        for (size_t i = 0; i < model_globals.size(); ++i)
        {
            world_globals[i] = model_globals[i] * m_entity_world_at_activate;
        }

        const int32_t hips = FindJointIndexAny(skeleton, { "hips", "pelvis", "Hips", "Pelvis" });
        const int32_t spine = FindJointIndexAny(
            skeleton,
            { "spine_01", "spine01", "spine1", "spine", "Spine", "Spine1", "spine.001" }
        );
        const int32_t chest = FindJointIndexAny(
            skeleton,
            {
                "spine_02", "spine02", "spine2", "chest", "Chest",
                "upperchest", "UpperChest", "Spine2", "spine.002"
            }
        );
        const int32_t neck = FindJointIndexAny(
            skeleton,
            { "neck_01", "neck01", "neck1", "neck", "Neck", "Neck1" }
        );
        const int32_t head = FindJointIndexAny(skeleton, { "head", "Head" });

        const int32_t thigh_l = FindJointIndexAny(skeleton, { "thigh.l", "upperleg.l", "UpperLeg.L", "LeftUpLeg", "mixamorig:LeftUpLeg" });
        const int32_t calf_l = FindJointIndexAny(skeleton, { "calf.l", "lowerleg.l", "LowerLeg.L", "LeftLeg", "mixamorig:LeftLeg" });
        const int32_t foot_l = FindJointIndexAny(skeleton, { "foot.l", "Foot.L", "LeftFoot", "mixamorig:LeftFoot" });
        const int32_t thigh_r = FindJointIndexAny(skeleton, { "thigh.r", "upperleg.r", "UpperLeg.R", "RightUpLeg", "mixamorig:RightUpLeg" });
        const int32_t calf_r = FindJointIndexAny(skeleton, { "calf.r", "lowerleg.r", "LowerLeg.R", "RightLeg", "mixamorig:RightLeg" });
        const int32_t foot_r = FindJointIndexAny(skeleton, { "foot.r", "Foot.R", "RightFoot", "mixamorig:RightFoot" });

        const int32_t arm_l = FindJointIndexAny(skeleton, { "upperarm.l", "UpperArm.L", "LeftArm", "mixamorig:LeftArm" });
        const int32_t forearm_l = FindJointIndexAny(skeleton, { "lowerarm.l", "LowerArm.L", "LeftForeArm", "mixamorig:LeftForeArm" });
        const int32_t arm_r = FindJointIndexAny(skeleton, { "upperarm.r", "UpperArm.R", "RightArm", "mixamorig:RightArm" });
        const int32_t forearm_r = FindJointIndexAny(skeleton, { "lowerarm.r", "LowerArm.R", "RightForeArm", "mixamorig:RightForeArm" });

        SP_LOG_INFO(
            "ragdoll map hips=%d spine=%d chest=%d neck=%d head=%d thigh_l=%d calf_l=%d foot_l=%d thigh_r=%d calf_r=%d foot_r=%d arm_l=%d forearm_l=%d arm_r=%d forearm_r=%d",
            hips, spine, chest, neck, head,
            thigh_l, calf_l, foot_l, thigh_r, calf_r, foot_r,
            arm_l, forearm_l, arm_r, forearm_r
        );

        if (hips < 0 || thigh_l < 0 || thigh_r < 0)
        {
            SP_LOG_WARNING(
                "Ragdoll: missing joints hips=%d thigh_l=%d thigh_r=%d arm_l=%d arm_r=%d",
                hips, thigh_l, thigh_r, arm_l, arm_r
            );
            string names;
            for (uint32_t i = 0; i < skeleton.joint_count; ++i)
            {
                if (i > 0)
                {
                    names += ", ";
                }
                names += skeleton.joint_names[i];
            }
            SP_LOG_WARNING("Ragdoll skeleton joints: %s", names.c_str());
            return false;
        }

        m_bodies.clear();
        m_joints.clear();

        const int32_t torso_child = chest >= 0 ? chest : (spine >= 0 ? spine : neck);
        const int32_t hips_index = hips >= 0 ? hips : 0;
        if (torso_child < 0 || torso_child == hips_index)
        {
            SP_LOG_ERROR("ragdoll missing torso joint, hips=%d spine=%d chest=%d neck=%d", hips, spine, chest, neck);
            return false;
        }

        // ~70kg total, limbs connected with d6 limits
        const int32_t hips_body = AddBoneBody(world_globals, hips_index, torso_child, 0.11f, 14.0f);
        const int32_t torso_body = AddBoneBody(
            world_globals,
            torso_child,
            neck >= 0 ? neck : head,
            0.10f,
            12.0f
        );
        const int32_t head_body = AddBoneBody(world_globals, head >= 0 ? head : neck, -1, 0.09f, 4.0f);

        const int32_t thigh_l_body = AddBoneBody(world_globals, thigh_l, calf_l, 0.08f, 8.0f);
        const int32_t calf_l_body = AddBoneBody(world_globals, calf_l, foot_l, 0.06f, 5.0f);
        const int32_t thigh_r_body = AddBoneBody(world_globals, thigh_r, calf_r, 0.08f, 8.0f);
        const int32_t calf_r_body = AddBoneBody(world_globals, calf_r, foot_r, 0.06f, 5.0f);

        const int32_t arm_l_body = AddBoneBody(world_globals, arm_l, forearm_l, 0.055f, 3.0f);
        const int32_t forearm_l_body = AddBoneBody(world_globals, forearm_l, -1, 0.045f, 2.0f);
        const int32_t arm_r_body = AddBoneBody(world_globals, arm_r, forearm_r, 0.055f, 3.0f);
        const int32_t forearm_r_body = AddBoneBody(world_globals, forearm_r, -1, 0.045f, 2.0f);

        if (m_bodies.size() < 4)
        {
            DestroyRagdoll();
            return false;
        }

        AddBoneJoint(hips_body, torso_body, 0.65f, 0.65f, 0.3f);
        AddBoneJoint(torso_body, head_body, 0.5f, 0.5f, 0.35f);
        AddBoneJoint(hips_body, thigh_l_body, 1.0f, 0.6f, 0.3f);
        AddBoneJoint(thigh_l_body, calf_l_body, 0.12f, 1.25f, 0.12f);
        AddBoneJoint(hips_body, thigh_r_body, 1.0f, 0.6f, 0.3f);
        AddBoneJoint(thigh_r_body, calf_r_body, 0.12f, 1.25f, 0.12f);
        AddBoneJoint(torso_body, arm_l_body, 1.15f, 1.15f, 0.7f);
        AddBoneJoint(arm_l_body, forearm_l_body, 0.12f, 1.35f, 0.2f);
        AddBoneJoint(torso_body, arm_r_body, 1.15f, 1.15f, 0.7f);
        AddBoneJoint(arm_r_body, forearm_r_body, 0.12f, 1.35f, 0.2f);

        Vector3 launch = hit_velocity;
        launch.y = clamp(launch.y * 0.2f + 1.0f, 0.5f, 2.5f);
        launch = clamp_vector_length(launch, max_launch_speed);
        const Vector3 angular(launch.z * 0.04f, 0.0f, -launch.x * 0.04f);

        for (BoneBody& body : m_bodies)
        {
            if (!body.actor)
            {
                continue;
            }

            body.actor->setLinearVelocity(PxVec3(launch.x, launch.y, launch.z));
            body.actor->setAngularVelocity(PxVec3(angular.x, angular.y, angular.z));
            body.actor->wakeUp();
        }

        return true;
    }

    void Ragdoll::SyncPoseFromActors()
    {
        if (!m_animator || m_bodies.empty())
        {
            return;
        }

        const Skeleton* skeleton = m_animator->GetSkeleton();
        if (!skeleton || skeleton->joint_count == 0)
        {
            return;
        }

        if (m_pose_locals.size() != skeleton->joint_count)
        {
            m_pose_locals = skeleton->bind_local_matrices;
        }

        const bool log_this_frame = m_debug_sync_count < 5;
        bool found_bad = false;

        // undriven bones keep animated locals, driven bones take physics globals
        const Matrix world_to_model = m_entity_world_at_activate.Inverted();
        vector<Matrix> model_globals(skeleton->joint_count, Matrix::Identity);
        vector<uint8_t> driven(skeleton->joint_count, 0);

        for (const BoneBody& body : m_bodies)
        {
            if (!body.actor || body.joint_index < 0 ||
                body.joint_index >= static_cast<int32_t>(model_globals.size()))
            {
                continue;
            }

            const Matrix actor_world = matrix_from_px(body.actor->getGlobalPose());
            const Matrix joint_world = body.actor_to_joint * actor_world;
            const Matrix joint_model = joint_world * world_to_model;
            if (!IsFiniteMatrix(joint_model) || matrix_looks_bad(joint_model))
            {
                found_bad = true;
                if (!m_debug_logged_bad || log_this_frame)
                {
                    SP_LOG_ERROR(
                        "ragdoll bad joint_model joint=%d sync_n=%u",
                        body.joint_index,
                        m_debug_sync_count
                    );
                    LogMatrix("  bad_actor_world", actor_world);
                    LogMatrix("  bad_actor_to_joint", body.actor_to_joint);
                    LogMatrix("  bad_joint_world", joint_world);
                    LogMatrix("  bad_joint_model", joint_model);
                }
                continue;
            }

            model_globals[static_cast<size_t>(body.joint_index)] = joint_model;
            driven[static_cast<size_t>(body.joint_index)] = 1;
        }

        // rebuild undriven globals so parents of driven bones match the new pelvis etc
        for (uint32_t i = 0; i < skeleton->joint_count; ++i)
        {
            if (driven[i])
            {
                continue;
            }

            const int16_t parent = skeleton->parent_indices[i];
            if (parent < 0)
            {
                model_globals[i] = m_pose_locals[i];
            }
            else
            {
                model_globals[i] = m_pose_locals[i] * model_globals[static_cast<size_t>(parent)];
            }
        }

        for (const BoneBody& body : m_bodies)
        {
            if (!body.actor || body.joint_index < 0 ||
                body.joint_index >= static_cast<int32_t>(skeleton->joint_count))
            {
                continue;
            }

            const uint32_t i = static_cast<uint32_t>(body.joint_index);
            const int16_t parent = skeleton->parent_indices[i];
            Matrix local;
            if (parent < 0)
            {
                local = model_globals[i];
            }
            else
            {
                const Matrix parent_global = model_globals[static_cast<size_t>(parent)];
                if (matrix_looks_bad(parent_global))
                {
                    found_bad = true;
                    if (!m_debug_logged_bad || log_this_frame)
                    {
                        SP_LOG_ERROR(
                            "ragdoll bad parent_global joint=%d parent=%d sync_n=%u",
                            body.joint_index,
                            parent,
                            m_debug_sync_count
                        );
                        LogMatrix("  bad_parent_global", parent_global);
                        LogMatrix("  bad_child_global", model_globals[i]);
                    }
                    continue;
                }

                local = model_globals[i] * parent_global.Inverted();
            }

            if (!IsFiniteMatrix(local) || matrix_looks_bad(local))
            {
                found_bad = true;
                if (!m_debug_logged_bad || log_this_frame)
                {
                    SP_LOG_ERROR(
                        "ragdoll bad local joint=%d parent=%d sync_n=%u",
                        body.joint_index,
                        parent,
                        m_debug_sync_count
                    );
                    LogMatrix("  bad_local", local);
                    LogMatrix("  child_global", model_globals[i]);
                    if (parent >= 0)
                    {
                        LogMatrix("  parent_global", model_globals[static_cast<size_t>(parent)]);
                    }
                }
                continue;
            }

            local = Matrix(local.GetTranslation(), local.GetRotation(), Vector3::One);
            m_pose_locals[i] = local;
        }

        if (log_this_frame || (found_bad && !m_debug_logged_bad))
        {
            LogSyncSample(found_bad ? "bad_matrix" : "sync_ok");
            if (found_bad)
            {
                m_debug_logged_bad = true;
            }
        }

        m_debug_sync_count++;
        m_animator->SetExternalPose(m_pose_locals);
        UpdateCullBounds();
    }

    void Ragdoll::ApplyCullBounds(const BoundingBox& world_box)
    {
        Entity* root = GetEntity();
        if (!root)
        {
            return;
        }

        vector<Entity*> nodes;
        nodes.push_back(root);
        root->GetDescendants(&nodes);

        for (Entity* node : nodes)
        {
            if (!node)
            {
                continue;
            }

            if (Render* render = node->GetComponent<Render>())
            {
                render->SetBoundingBoxOverride(world_box);
            }
        }
    }

    void Ragdoll::UpdateCullBounds()
    {
        if (m_bodies.empty())
        {
            if (m_cull_bounds_valid)
            {
                ApplyCullBounds(m_cull_bounds_world);
            }
            return;
        }

        vector<Vector3> points;
        points.reserve(m_bodies.size());
        for (const BoneBody& body : m_bodies)
        {
            if (!body.actor)
            {
                continue;
            }

            const PxTransform pose = body.actor->getGlobalPose();
            points.emplace_back(pose.p.x, pose.p.y, pose.p.z);
        }

        if (points.empty())
        {
            return;
        }

        // pad so mesh volume around capsules stays inside the cull aabb
        constexpr float pad = 0.75f;
        BoundingBox box(points.data(), static_cast<uint32_t>(points.size()));
        box = BoundingBox(
            box.GetMin() - Vector3(pad, pad, pad),
            box.GetMax() + Vector3(pad, pad, pad)
        );

        m_cull_bounds_world = box;
        m_cull_bounds_valid = true;
        ApplyCullBounds(box);
    }

    bool Ragdoll::AreBodiesSleeping() const
    {
        if (m_bodies.empty())
        {
            return true;
        }

        for (const BoneBody& body : m_bodies)
        {
            if (body.actor && !body.actor->isSleeping())
            {
                return false;
            }
        }

        return true;
    }

    void Ragdoll::Freeze()
    {
        SyncPoseFromActors();

        // drop joints and park limbs as kinematic, cheap until woken by a hit or pick
        DestroyJoints();
        {
            lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
            for (BoneBody& body : m_bodies)
            {
                if (!body.actor)
                {
                    continue;
                }

                body.actor->setLinearVelocity(PxVec3(0.0f, 0.0f, 0.0f));
                body.actor->setAngularVelocity(PxVec3(0.0f, 0.0f, 0.0f));
                body.actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
            }
        }

        m_state = State::Frozen;
        m_sleep_timer = 0.0f;

        if (m_animator && !m_pose_locals.empty())
        {
            m_animator->SetExternalPose(m_pose_locals);
        }

        UpdateCullBounds();
    }

    bool Ragdoll::Wake(const Vector3& hit_position, const Vector3& hit_velocity)
    {
        if (m_state != State::Frozen)
        {
            return m_state == State::Simulating;
        }

        if (m_bodies.empty())
        {
            if (!m_animator)
            {
                m_animator = GetEntity()->GetComponent<Animator>();
            }

            const Skeleton* skeleton = m_animator ? m_animator->GetSkeleton() : nullptr;
            if (!skeleton || m_pose_locals.size() != skeleton->joint_count)
            {
                return false;
            }

            if (!BuildRagdoll(*skeleton, m_pose_locals, hit_velocity))
            {
                return false;
            }

            m_state = State::Simulating;
            m_sleep_timer = 0.0f;
            m_animator->SetExternalPose(m_pose_locals);
            UpdateCullBounds();
            return true;
        }

        Vector3 launch = hit_velocity;
        if (launch.LengthSquared() < 0.01f)
        {
            launch = Vector3(0.0f, 1.5f, 0.0f);
        }
        launch.y = clamp(launch.y * 0.2f + 1.0f, 0.5f, 2.5f);
        launch = clamp_vector_length(launch, max_launch_speed);
        const Vector3 angular(launch.z * 0.04f, 0.0f, -launch.x * 0.04f);

        {
            lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
            for (BoneBody& body : m_bodies)
            {
                if (!body.actor)
                {
                    continue;
                }

                body.actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, false);

                const PxTransform pose = body.actor->getGlobalPose();
                Vector3 to_body(
                    pose.p.x - hit_position.x,
                    pose.p.y - hit_position.y,
                    pose.p.z - hit_position.z
                );
                const float distance = to_body.Length();
                const float weight = distance < 0.01f
                    ? 1.0f
                    : clamp(1.0f / (1.0f + distance), 0.35f, 1.0f);

                body.actor->setLinearVelocity(PxVec3(
                    launch.x * weight,
                    launch.y * weight,
                    launch.z * weight
                ));
                body.actor->setAngularVelocity(PxVec3(
                    angular.x * weight,
                    angular.y * weight,
                    angular.z * weight
                ));
                body.actor->wakeUp();
            }
        }

        RecreateJoints();
        m_state = State::Simulating;
        m_sleep_timer = 0.0f;
        UpdateCullBounds();
        return true;
    }

    void Ragdoll::RegisterForScripting(sol::state_view state)
    {
        state.new_usertype<Ragdoll>("Ragdoll",
            sol::base_classes, sol::bases<Component>(),
            "IsDead", &Ragdoll::IsDead,
            "IsFrozen", &Ragdoll::IsFrozen,
            "Activate", &Ragdoll::Activate,
            "Wake", &Ragdoll::Wake,
            "SetHitBodyEnabled", &Ragdoll::SetHitBodyEnabled,
            "IsHitBodyEnabled", &Ragdoll::IsHitBodyEnabled
        );
    }

    sol::reference Ragdoll::AsLua(sol::state_view state)
    {
        return sol::make_reference(state, this);
    }
}
