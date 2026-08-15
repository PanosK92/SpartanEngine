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
#include "../../animation/Skeleton.h"
#include "../../animation/SkeletalMeshBinding.h"
#include "../../geometry/Mesh.h"
#include "../../math/BoundingBox.h"
#include "../../core/Engine.h"
#include "../../core/Timer.h"
#include "../../math/Quaternion.h"
#include "../../physics/PhysicsWorld.h"
#include "../../rendering/Renderer.h"
#include "../../rendering/Color.h"
#include "../Entity.h"
#include <cctype>
#include <cmath>

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
        constexpr float max_body_speed = 40.0f;
        constexpr float max_launch_speed = 14.0f;
        constexpr float hit_speed = 3.5f;
        constexpr float hit_impulse = 40.0f;
        constexpr float hit_impulse_mass = 70.0f;
        constexpr float hit_body_mass = 70.0f;
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
            while (entity)
            {
                if (Physics* physics = entity->GetComponent<Physics>())
                {
                    return physics->GetLinearVelocity();
                }

                entity = entity->GetParent();
            }

            return Vector3::Zero;
        }

        // small lift + cap, velocity itself comes from the dynamic hit body / hitter
        Vector3 shape_launch_velocity(Vector3 launch)
        {
            const float horizontal = sqrtf(launch.x * launch.x + launch.z * launch.z);
            if (launch.y < horizontal * 0.05f)
            {
                launch.y = max(launch.y, min(horizontal * 0.12f, 2.0f));
            }

            return clamp_vector_length(launch, max_launch_speed);
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

        void draw_debug_marker(const Vector3& point, float size, const Color& color)
        {
            Renderer::DrawLine(
                point - Vector3(size, 0.0f, 0.0f),
                point + Vector3(size, 0.0f, 0.0f),
                color,
                color
            );
            Renderer::DrawLine(
                point - Vector3(0.0f, size, 0.0f),
                point + Vector3(0.0f, size, 0.0f),
                color,
                color
            );
            Renderer::DrawLine(
                point - Vector3(0.0f, 0.0f, size),
                point + Vector3(0.0f, 0.0f, size),
                color,
                color
            );
        }

        void draw_debug_capsule(
            const Vector3& center,
            const Quaternion& rotation,
            float radius,
            float half_height,
            const Color& color
        )
        {
            // physx capsules are along local x
            const Vector3 axis = (rotation * Vector3::Right).Normalized();
            Vector3 tangent = axis.Cross(Vector3::Up);
            if (tangent.LengthSquared() < 1.0e-6f)
            {
                tangent = axis.Cross(Vector3::Right);
            }
            tangent.Normalize();
            const Vector3 bitangent = axis.Cross(tangent).Normalized();

            const Vector3 start = center - axis * half_height;
            const Vector3 end = center + axis * half_height;
            constexpr int ring_segments = 12;
            constexpr int length_rings = 3;

            for (int ring = 0; ring <= length_rings; ++ring)
            {
                const float t = static_cast<float>(ring) / static_cast<float>(length_rings);
                const Vector3 ring_center = start + (end - start) * t;
                Vector3 previous;
                for (int i = 0; i <= ring_segments; ++i)
                {
                    const float angle =
                        static_cast<float>(i) * 6.2831853f / static_cast<float>(ring_segments);
                    const Vector3 point = ring_center +
                        (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                    if (i > 0)
                    {
                        Renderer::DrawLine(previous, point, color, color);
                    }
                    previous = point;
                }
            }

            for (int stripe = 0; stripe < 4; ++stripe)
            {
                const float angle = static_cast<float>(stripe) * 1.5707963f;
                const Vector3 offset =
                    (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                Renderer::DrawLine(start + offset, end + offset, color, color);
            }

            // end caps as rings only, full spheres read as extra colliders
            for (int cap = 0; cap < 2; ++cap)
            {
                const Vector3 cap_center = cap == 0 ? start : end;
                const Vector3 along = cap == 0 ? -axis : axis;
                Vector3 previous;
                for (int i = 0; i <= ring_segments; ++i)
                {
                    const float angle =
                        static_cast<float>(i) * 6.2831853f / static_cast<float>(ring_segments);
                    const Vector3 radial =
                        (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                    const Vector3 point = cap_center + radial;
                    if (i > 0)
                    {
                        Renderer::DrawLine(previous, point, color, color);
                    }
                    previous = point;
                }
                Renderer::DrawLine(cap_center, cap_center + along * radius, color, color);
            }
        }

        void draw_debug_actor_capsules(PxRigidDynamic* actor, const Color& color)
        {
            if (!actor)
            {
                return;
            }

            PxShape* shapes[8];
            const PxU32 count = actor->getShapes(shapes, 8);
            for (PxU32 i = 0; i < count; ++i)
            {
                if (!shapes[i] ||
                    shapes[i]->getGeometry().getType() != PxGeometryType::eCAPSULE)
                {
                    continue;
                }

                const PxCapsuleGeometry& capsule =
                    static_cast<const PxCapsuleGeometry&>(shapes[i]->getGeometry());
                const PxTransform world = actor->getGlobalPose() * shapes[i]->getLocalPose();
                draw_debug_capsule(
                    Vector3(world.p.x, world.p.y, world.p.z),
                    Quaternion(world.q.x, world.q.y, world.q.z, world.q.w),
                    capsule.radius,
                    capsule.halfHeight,
                    color
                );
            }
        }
    }

    Ragdoll::Ragdoll(Entity* entity) : Component(entity)
    {
        m_hit_center_offset = Vector3(0.0f, hit_half_height + hit_radius, 0.0f);
        m_hit_body_wanted = false;
    }

    Ragdoll::~Ragdoll()
    {
        // skip ResetToAlive render/lod work, entity teardown can run after meshes are gone
        DestroyRagdoll();
        DestroyHitBody();
        if (m_material)
        {
            m_material->release();
            m_material = nullptr;
        }
        m_animator = nullptr;
        m_state = State::Alive;
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
            // play-stop must restore the mesh immediately, not on the next Start
            m_animator->ApplyBindPose();
        }

        m_state = State::Alive;
        m_sleep_timer = 0.0f;
        m_pose_locals.clear();
        m_cull_bounds_valid = false;
        m_entity_world_at_activate = Matrix::Identity;
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

    void Ragdoll::LateTick()
    {
        if (m_state == State::Alive && m_hit_body_wanted)
        {
            if (!m_hit_body)
            {
                CreateHitBody();
            }
            SyncHitBody();
        }

        if (cvar_ragdoll.GetValueAs<bool>())
        {
            DrawDebug();
        }
    }

    void Ragdoll::DrawDebug() const
    {
        if (!m_hit_body && m_bodies.empty())
        {
            return;
        }

        const Color body_color = m_state == State::Frozen
            ? Color(0.35f, 0.65f, 1.0f, 1.0f)
            : Color(1.0f, 0.35f, 0.15f, 1.0f);
        const Color joint_color(0.2f, 1.0f, 0.35f, 1.0f);
        const Color link_color(1.0f, 0.9f, 0.2f, 1.0f);
        const Color hit_color(0.2f, 0.9f, 1.0f, 1.0f);

        lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());

        if (m_hit_body)
        {
            draw_debug_actor_capsules(m_hit_body, hit_color);
        }

        for (const BoneBody& body : m_bodies)
        {
            if (!body.actor)
            {
                continue;
            }

            draw_debug_actor_capsules(body.actor, body_color);

            // bone origin driven into the mesh, green = sync point
            const Matrix joint_world =
                body.actor_to_joint * matrix_from_px(body.actor->getGlobalPose());
            draw_debug_marker(joint_world.GetTranslation(), 0.04f, joint_color);
        }

        for (const BoneJoint& joint : m_joints)
        {
            if (joint.parent_body < 0 || joint.child_body < 0 ||
                joint.parent_body >= static_cast<int32_t>(m_bodies.size()) ||
                joint.child_body >= static_cast<int32_t>(m_bodies.size()))
            {
                continue;
            }

            const BoneBody& parent = m_bodies[static_cast<size_t>(joint.parent_body)];
            const BoneBody& child = m_bodies[static_cast<size_t>(joint.child_body)];
            if (!parent.actor || !child.actor)
            {
                continue;
            }

            const Vector3 parent_pos =
                (parent.actor_to_joint * matrix_from_px(parent.actor->getGlobalPose()))
                    .GetTranslation();
            const Vector3 child_pos =
                (child.actor_to_joint * matrix_from_px(child.actor->getGlobalPose()))
                    .GetTranslation();
            Renderer::DrawLine(parent_pos, child_pos, link_color, link_color);
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
            return;
        }

        const Skeleton* skeleton = m_animator->GetSkeleton();
        if (!skeleton || skeleton->joint_count == 0)
        {
            return;
        }

        vector<Matrix> locals = m_animator->GetCurrentLocalPose();
        if (locals.size() != skeleton->joint_count)
        {
            locals = skeleton->bind_local_matrices;
        }
        if (locals.size() != skeleton->joint_count)
        {
            return;
        }

        // lock entity transform, all motion comes from joint locals after this
        Entity* entity = GetEntity();
        m_entity_world_at_activate = Matrix(
            entity->GetPosition(),
            entity->GetRotation(),
            entity->GetScale()
        );

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

        if (!BuildRagdoll(*skeleton, locals, launch, hit_position))
        {
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

        Vector3 center;
        Quaternion rotation;
        float radius = hit_radius;
        float half_height = hit_half_height;
        ComputeHitBodyWorld(center, rotation, radius, half_height);

        lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());

        if (!m_material)
        {
            m_material = physics->createMaterial(0.55f, 0.45f, 0.05f);
        }

        Entity* entity = GetEntity();
        PxRigidDynamic* actor = physics->createRigidDynamic(to_px(center, rotation));
        if (!actor)
        {
            return;
        }

        PxCapsuleGeometry geometry(radius, half_height);
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

        // normal dynamic human mass, physx resolves car hits
        PxRigidBodyExt::setMassAndUpdateInertia(*actor, hit_body_mass);
        actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, false);
        actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        actor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
        actor->setLinearDamping(0.4f);
        actor->setAngularDamping(2.0f);
        actor->setMaxLinearVelocity(max_body_speed);
        actor->setMaxAngularVelocity(20.0f);
        actor->setMaxDepenetrationVelocity(1.5f);
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

    bool Ragdoll::ComputeHitBodyWorld(
        Vector3& out_center,
        Quaternion& out_rotation,
        float& out_radius,
        float& out_half_height
    ) const
    {
        Entity* entity = GetEntity();
        if (!entity)
        {
            return false;
        }

        out_rotation = entity->GetRotation();
        out_center = entity->GetPosition() + out_rotation * m_hit_center_offset;
        out_radius = hit_radius;
        out_half_height = hit_half_height;

        Animator* animator = m_animator
            ? m_animator
            : entity->GetComponent<Animator>();
        if (!animator)
        {
            return true;
        }

        const Skeleton* skeleton = animator->GetSkeleton();
        const vector<Matrix>& locals = animator->GetCurrentLocalPose();
        if (!skeleton ||
            skeleton->joint_count == 0 ||
            locals.size() != skeleton->joint_count)
        {
            return true;
        }

        vector<Matrix> model_globals(skeleton->joint_count);
        skeleton->ComputeGlobalPose(locals, model_globals);

        const Matrix entity_world = entity->GetMatrix();
        auto joint_world = [&](const int32_t index) -> Vector3
        {
            return entity_world *
                model_globals[static_cast<size_t>(index)].GetTranslation();
        };

        const int32_t hips = FindJointIndexAny(
            *skeleton,
            { "hips", "pelvis", "Hips", "Pelvis" }
        );
        const int32_t head = FindJointIndexAny(*skeleton, { "head", "Head" });
        const int32_t foot_l = FindJointIndexAny(
            *skeleton,
            { "foot.l", "Foot.L", "LeftFoot", "mixamorig:LeftFoot" }
        );
        const int32_t foot_r = FindJointIndexAny(
            *skeleton,
            { "foot.r", "Foot.R", "RightFoot", "mixamorig:RightFoot" }
        );

        if (hips < 0)
        {
            return true;
        }

        const Vector3 hips_w = joint_world(hips);
        Vector3 head_w = hips_w + Vector3(0.0f, 0.7f, 0.0f);
        if (head >= 0)
        {
            head_w = joint_world(head);
        }

        float feet_y = hips_w.y - 0.9f;
        bool has_feet = false;
        if (foot_l >= 0)
        {
            feet_y = joint_world(foot_l).y;
            has_feet = true;
        }
        if (foot_r >= 0)
        {
            const float y = joint_world(foot_r).y;
            feet_y = has_feet ? min(feet_y, y) : y;
            has_feet = true;
        }

        // xz follows hips through animated lean/stride, y spans feet to head
        out_center = Vector3(
            hips_w.x,
            (feet_y + head_w.y) * 0.5f,
            hips_w.z
        );

        Vector3 forward = entity->GetForward();
        forward.y = 0.0f;
        if (forward.LengthSquared() > 1.0e-6f)
        {
            out_rotation = Quaternion::FromLookRotation(forward.Normalized(), Vector3::Up);
        }

        const float span = clamp(head_w.y - feet_y, 0.7f, 2.2f);
        out_radius = clamp(span * 0.16f, 0.18f, 0.32f);
        out_half_height = max(0.12f, span * 0.5f - out_radius);
        return true;
    }

    void Ragdoll::SyncHitBody() const
    {
        if (!m_hit_body || !GetEntity())
        {
            return;
        }

        Vector3 center;
        Quaternion rotation;
        float radius = hit_radius;
        float half_height = hit_half_height;
        if (!ComputeHitBodyWorld(center, rotation, radius, half_height))
        {
            return;
        }

        const PxTransform pose = to_px(center, rotation);

        lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
        // follow the walker between steps, leave velocity clear so the next phys step owns hits
        m_hit_body->setGlobalPose(pose);
        m_hit_body->setLinearVelocity(PxVec3(0.0f, 0.0f, 0.0f));
        m_hit_body->setAngularVelocity(PxVec3(0.0f, 0.0f, 0.0f));

        PxShape* shapes[4];
        const PxU32 count = m_hit_body->getShapes(shapes, 4);
        for (PxU32 i = 0; i < count; ++i)
        {
            if (!shapes[i] ||
                shapes[i]->getGeometry().getType() != PxGeometryType::eCAPSULE)
            {
                continue;
            }

            shapes[i]->setGeometry(PxCapsuleGeometry(radius, half_height));
        }
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

            // prefer the dynamic capsule velocity after physx resolved the hit
            Vector3 launch = Vector3::Zero;
            if (m_hit_body)
            {
                const PxVec3 body_v = m_hit_body->getLinearVelocity();
                launch = Vector3(body_v.x, body_v.y, body_v.z);
            }

            const Vector3 other_to_self = self->GetPosition() - other->GetPosition();
            Vector3 hitter_v = actor_linear_velocity(other);
            const float hitter_speed = hitter_v.Length();
            if (launch.LengthSquared() < hit_speed * hit_speed && hitter_speed >= hit_speed)
            {
                Vector3 away = other_to_self;
                if (away.LengthSquared() < 0.0001f && normal.LengthSquared() > 0.0001f)
                {
                    away = normal;
                }
                if (away.LengthSquared() > 0.0001f && Vector3::Dot(hitter_v, away) < 0.0f)
                {
                    hitter_v = away.Normalized() * hitter_speed;
                }
                // light body leaves near hitter speed, soft transfer so cubes are not soft
                launch = hitter_v * 0.55f;
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
                    const float from_impulse = clamp(
                        impulse_mag / hit_impulse_mass,
                        0.0f,
                        max_launch_speed
                    );
                    launch = impulse.Normalized() * from_impulse;
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

            Activate(hit_position, shape_launch_velocity(launch));
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

    float Ragdoll::EstimateBoneRadius(
        const Skeleton& skeleton,
        const vector<Matrix>& model_globals,
        int32_t joint_index,
        int32_t child_joint_index,
        float fallback_radius
    ) const
    {
        if (joint_index < 0 || joint_index >= static_cast<int32_t>(model_globals.size()))
        {
            return fallback_radius;
        }

        Entity* entity = GetEntity();
        Render* render = entity ? entity->GetComponent<Render>() : nullptr;
        Mesh* mesh = render ? render->GetMesh() : nullptr;
        const SkeletalMeshBinding* binding = mesh ? mesh->GetSkeletalMeshBinding() : nullptr;
        if (!mesh || !binding || mesh->GetVertices().empty())
        {
            return fallback_radius;
        }

        // bones on the joint->child chain so mid-spine verts count for the torso
        uint8_t bone_mask_stack[256] = {};
        const uint32_t joint_count = skeleton.joint_count;
        auto mark_bone = [&](int32_t bone)
        {
            if (bone >= 0 && static_cast<uint32_t>(bone) < joint_count && bone < 256)
            {
                bone_mask_stack[bone] = 1;
            }
        };
        mark_bone(joint_index);
        mark_bone(child_joint_index);
        if (child_joint_index >= 0 &&
            static_cast<uint32_t>(child_joint_index) < joint_count)
        {
            int32_t walk = skeleton.parent_indices[static_cast<uint32_t>(child_joint_index)];
            uint32_t guard = 0;
            while (walk >= 0 &&
                   walk != joint_index &&
                   static_cast<uint32_t>(walk) < joint_count &&
                   guard < joint_count)
            {
                mark_bone(walk);
                walk = skeleton.parent_indices[static_cast<uint32_t>(walk)];
                ++guard;
            }
        }

        const Vector3 start = model_globals[static_cast<size_t>(joint_index)].GetTranslation();
        Vector3 end = start;
        if (child_joint_index >= 0 &&
            child_joint_index < static_cast<int32_t>(model_globals.size()))
        {
            end = model_globals[static_cast<size_t>(child_joint_index)].GetTranslation();
        }
        else
        {
            end = start + Vector3(0.0f, max(0.12f, fallback_radius * 1.5f), 0.0f);
        }

        Vector3 axis = end - start;
        float axis_len = axis.Length();
        const bool use_axis = axis_len > 1.0e-4f;
        if (use_axis)
        {
            axis /= axis_len;
        }

        const vector<RHI_Vertex_PosTexNorTan>& vertices =
            const_cast<Mesh*>(mesh)->GetVertices();
        float max_radius = 0.0f;
        uint32_t samples = 0;

        for (const SkeletalMeshSection& section : binding->GetSections())
        {
            if (section.influences.size() != section.vertex_count ||
                section.vertex_input_offset + section.vertex_count > vertices.size())
            {
                continue;
            }

            for (uint32_t i = 0; i < section.vertex_count; ++i)
            {
                const SkeletalVertexInfluence& influence = section.influences[i];
                float weight = 0.0f;
                for (uint32_t w = 0; w < 4; ++w)
                {
                    const int32_t bone = static_cast<int32_t>(influence.bone_indices[w]);
                    if (bone >= 0 && bone < 256 && bone_mask_stack[bone])
                    {
                        weight += influence.bone_weights[w];
                    }
                }
                if (weight < 0.2f)
                {
                    continue;
                }

                const Vector3 pos = vertices[section.vertex_input_offset + i].get_position();
                float radial = 0.0f;
                if (use_axis)
                {
                    const Vector3 delta = pos - start;
                    const float t = delta.Dot(axis);
                    radial = (delta - axis * t).Length();
                }
                else
                {
                    radial = (pos - start).Length();
                }

                max_radius = max(max_radius, radial);
                ++samples;
            }
        }

        if (samples < 12)
        {
            return fallback_radius;
        }

        // slight pad so the collider sits on the mesh surface, not under it
        return clamp(max_radius * 1.06f, fallback_radius * 0.75f, max(fallback_radius * 3.5f, 0.28f));
    }

    int32_t Ragdoll::AddBoneBody(
        const vector<Matrix>& world_globals,
        int32_t joint_index,
        int32_t child_joint_index,
        float radius,
        float mass,
        float angular_damping,
        float max_angular_speed,
        float min_length,
        float max_length
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
            // tip bones: head uses up, hands/feet keep facing
            dir = Vector3::Up;
            if (child_joint_index < 0)
            {
                const Vector3 local_y = joint_world.GetRotation() * Vector3::Up;
                if (local_y.LengthSquared() > 1.0e-8f)
                {
                    dir = local_y.Normalized();
                }
            }
            else
            {
                dir = m_entity_world_at_activate.GetRotation() * Vector3(0.0f, 0.0f, -1.0f);
                if (dir.LengthSquared() < 1.0e-8f)
                {
                    dir = joint_world.GetRotation() * Vector3(0.0f, 0.0f, -1.0f);
                }
                if (dir.LengthSquared() < 1.0e-8f)
                {
                    dir = Vector3(0.0f, 0.0f, -1.0f);
                }
                dir.Normalize();
            }
            length = min_length;
        }
        else
        {
            dir /= length;
        }

        length = clamp(length, min_length, max_length);
        // grow the bone span if the mesh needs a fatter capsule than the joint distance
        if (radius * 2.05f > length)
        {
            length = min(radius * 2.05f, max_length);
        }
        // was 0.35, that forced torso/head into skinny capsules inside the mesh
        radius = min(radius, length * 0.49f);
        const float half_height = max(0.01f, (length * 0.5f) - radius);
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
        actor->setSolverIterationCounts(8, 2);
        actor->setMaxLinearVelocity(max_body_speed);
        actor->setMaxAngularVelocity(max_angular_speed);
        actor->setLinearDamping(0.12f);
        actor->setAngularDamping(angular_damping);
        actor->userData = reinterpret_cast<void*>(GetEntity());
        tag_shapes(actor, physics_collision_ragdoll);
        PhysicsWorld::AddActor(actor);

        BoneBody body;
        body.actor = actor;
        body.joint_index = joint_index;
        // joint in actor space, row-vector: joint_world = actor_to_joint * actor_world
        body.actor_to_joint = joint_world * actor_world.Inverted();
        m_bodies.push_back(body);

        return static_cast<int32_t>(m_bodies.size() - 1);
    }

    int32_t Ragdoll::AddFootBody(
        const vector<Matrix>& world_globals,
        int32_t foot_index,
        int32_t ball_index,
        float mass
    )
    {
        if (foot_index < 0 || foot_index >= static_cast<int32_t>(world_globals.size()))
        {
            return -1;
        }

        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        if (!physics || !m_material)
        {
            return -1;
        }

        const Matrix& foot_world = world_globals[static_cast<size_t>(foot_index)];
        const Vector3 ankle = foot_world.GetTranslation();

        Vector3 toe_dir = m_entity_world_at_activate.GetRotation() * Vector3(0.0f, 0.0f, -1.0f);
        float length = 0.18f;
        if (ball_index >= 0 && ball_index < static_cast<int32_t>(world_globals.size()))
        {
            Vector3 to_ball =
                world_globals[static_cast<size_t>(ball_index)].GetTranslation() - ankle;
            const float ball_len = to_ball.Length();
            if (ball_len > 0.04f)
            {
                toe_dir = to_ball / ball_len;
                // a bit past the ball so the capsule covers the toes
                length = clamp(ball_len * 1.2f, 0.14f, 0.26f);
            }
        }
        if (toe_dir.LengthSquared() < 1.0e-8f)
        {
            toe_dir = Vector3(0.0f, 0.0f, -1.0f);
        }
        toe_dir.Normalize();

        float radius = 0.05f;
        if (m_animator && m_animator->GetSkeleton())
        {
            const Matrix world_to_model = m_entity_world_at_activate.Inverted();
            vector<Matrix> model_from_world(world_globals.size());
            for (size_t i = 0; i < world_globals.size(); ++i)
            {
                model_from_world[i] = world_globals[i] * world_to_model;
            }
            const Vector3 entity_scale = m_entity_world_at_activate.GetScale();
            const float scale_avg = max(
                0.01f,
                (fabsf(entity_scale.x) + fabsf(entity_scale.y) + fabsf(entity_scale.z)) / 3.0f
            );
            radius = EstimateBoneRadius(
                *m_animator->GetSkeleton(),
                model_from_world,
                foot_index,
                ball_index,
                0.05f
            ) * scale_avg;
        }
        if (radius * 2.05f > length)
        {
            length = radius * 2.05f;
        }
        radius = min(radius, length * 0.49f);
        const float half_height = max(0.03f, length * 0.5f - radius);
        const Vector3 center = ankle + toe_dir * (length * 0.5f);
        const Quaternion rotation = Quaternion::FromRotation(Vector3::Right, toe_dir);
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
        actor->setSolverIterationCounts(8, 2);
        actor->setMaxLinearVelocity(max_body_speed);
        actor->setMaxAngularVelocity(6.0f);
        actor->setLinearDamping(0.12f);
        actor->setAngularDamping(3.0f);
        actor->userData = reinterpret_cast<void*>(GetEntity());
        tag_shapes(actor, physics_collision_ragdoll);
        PhysicsWorld::AddActor(actor);

        BoneBody body;
        body.actor = actor;
        body.joint_index = foot_index;
        body.actor_to_joint = foot_world * actor_world.Inverted();
        m_bodies.push_back(body);

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
        const BoneBody& body_a = m_bodies[static_cast<size_t>(parent_body)];
        const BoneBody& body_b = m_bodies[static_cast<size_t>(child_body)];
        PxRigidDynamic* actor_a = body_a.actor;
        PxRigidDynamic* actor_b = body_b.actor;
        if (!physics || !actor_a || !actor_b)
        {
            return;
        }

        const PxTransform pose_a = actor_a->getGlobalPose();
        const PxTransform pose_b = actor_b->getGlobalPose();
        const Matrix parent_joint_world =
            body_a.actor_to_joint * matrix_from_px(pose_a);
        const Matrix child_joint_world =
            body_b.actor_to_joint * matrix_from_px(pose_b);
        const Vector3 anchor = child_joint_world.GetTranslation();
        const Vector3 parent_pos = parent_joint_world.GetTranslation();

        // capsule x is the bone axis, keep twist along the child limb
        Vector3 axis_x = matrix_from_px(pose_b).GetRotation() * Vector3::Right;
        if (axis_x.LengthSquared() < 1.0e-8f)
        {
            axis_x = Vector3::Up;
        }
        axis_x.Normalize();

        Vector3 parent_dir = anchor - parent_pos;
        if (parent_dir.LengthSquared() < 1.0e-8f)
        {
            parent_dir = matrix_from_px(pose_a).GetRotation() * Vector3::Right;
        }
        if (parent_dir.LengthSquared() < 1.0e-8f)
        {
            parent_dir = Vector3::Up;
        }
        parent_dir.Normalize();

        // z = hinge, matches large swing_z on knees and elbows
        Vector3 axis_z = parent_dir.Cross(axis_x);
        if (axis_z.LengthSquared() < 1.0e-6f)
        {
            const Vector3 ref = fabsf(axis_x.Dot(Vector3::Up)) < 0.9f
                ? Vector3::Up
                : Vector3::Right;
            axis_z = axis_x.Cross(ref);
        }
        if (axis_z.LengthSquared() < 1.0e-8f)
        {
            axis_z = Vector3::Forward;
        }
        axis_z.Normalize();

        Vector3 axis_y = axis_z.Cross(axis_x);
        if (axis_y.LengthSquared() < 1.0e-8f)
        {
            axis_y = Vector3::Up;
        }
        axis_y.Normalize();
        axis_z = axis_x.Cross(axis_y).Normalized();

        Quaternion frame_rot;
        frame_rot.FromAxes(axis_x, axis_y, axis_z);

        const PxTransform world_frame(
            PxVec3(anchor.x, anchor.y, anchor.z),
            PxQuat(frame_rot.x, frame_rot.y, frame_rot.z, frame_rot.w)
        );
        const PxTransform frame_a = pose_a.transformInv(world_frame);
        const PxTransform frame_b = pose_b.transformInv(world_frame);

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
        const Vector3& hit_velocity,
        const Vector3& hit_position
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
        const int32_t ball_l = FindJointIndexAny(
            skeleton,
            { "ball.l", "Ball.L", "LeftToeBase", "mixamorig:LeftToeBase", "toe.l", "Toe.L" }
        );
        const int32_t thigh_r = FindJointIndexAny(skeleton, { "thigh.r", "upperleg.r", "UpperLeg.R", "RightUpLeg", "mixamorig:RightUpLeg" });
        const int32_t calf_r = FindJointIndexAny(skeleton, { "calf.r", "lowerleg.r", "LowerLeg.R", "RightLeg", "mixamorig:RightLeg" });
        const int32_t foot_r = FindJointIndexAny(skeleton, { "foot.r", "Foot.R", "RightFoot", "mixamorig:RightFoot" });
        const int32_t ball_r = FindJointIndexAny(
            skeleton,
            { "ball.r", "Ball.R", "RightToeBase", "mixamorig:RightToeBase", "toe.r", "Toe.R" }
        );

        const int32_t arm_l = FindJointIndexAny(skeleton, { "upperarm.l", "UpperArm.L", "LeftArm", "mixamorig:LeftArm" });
        const int32_t forearm_l = FindJointIndexAny(skeleton, { "lowerarm.l", "LowerArm.L", "LeftForeArm", "mixamorig:LeftForeArm" });
        const int32_t hand_l = FindJointIndexAny(
            skeleton,
            { "hand.l", "Hand.L", "LeftHand", "mixamorig:LeftHand" }
        );
        const int32_t arm_r = FindJointIndexAny(skeleton, { "upperarm.r", "UpperArm.R", "RightArm", "mixamorig:RightArm" });
        const int32_t forearm_r = FindJointIndexAny(skeleton, { "lowerarm.r", "LowerArm.R", "RightForeArm", "mixamorig:RightForeArm" });
        const int32_t hand_r = FindJointIndexAny(
            skeleton,
            { "hand.r", "Hand.R", "RightHand", "mixamorig:RightHand" }
        );

        if (hips < 0 || thigh_l < 0 || thigh_r < 0)
        {
            return false;
        }

        m_bodies.clear();
        m_joints.clear();

        const int32_t torso_child = chest >= 0 ? chest : (spine >= 0 ? spine : neck);
        const int32_t hips_index = hips >= 0 ? hips : 0;
        if (torso_child < 0 || torso_child == hips_index)
        {
            return false;
        }

        // radii from skinned mesh around each bone, then scaled to world
        const Vector3 entity_scale = m_entity_world_at_activate.GetScale();
        const float scale_avg = max(
            0.01f,
            (fabsf(entity_scale.x) + fabsf(entity_scale.y) + fabsf(entity_scale.z)) / 3.0f
        );
        auto radius_world = [&](int32_t joint, int32_t child, float fallback) -> float
        {
            return EstimateBoneRadius(skeleton, model_globals, joint, child, fallback) * scale_avg;
        };

        const int32_t head_joint = head >= 0 ? head : neck;
        const int32_t torso_end = neck >= 0 ? neck : head;
        const float hips_r = radius_world(hips_index, torso_child, 0.14f);
        const float torso_r = radius_world(torso_child, torso_end, 0.15f);
        const float head_r = radius_world(head_joint, -1, 0.12f);
        const float thigh_r_l = radius_world(thigh_l, calf_l, 0.09f);
        const float calf_r_l = radius_world(calf_l, foot_l, 0.06f);
        const float thigh_r_r = radius_world(thigh_r, calf_r, 0.09f);
        const float calf_r_r = radius_world(calf_r, foot_r, 0.06f);
        const float arm_r_l = radius_world(arm_l, forearm_l, 0.055f);
        const float forearm_r_l = radius_world(forearm_l, hand_l, 0.045f);
        const float hand_r_l = radius_world(hand_l, -1, 0.04f);
        const float arm_r_r = radius_world(arm_r, forearm_r, 0.055f);
        const float forearm_r_r = radius_world(forearm_r, hand_r, 0.045f);
        const float hand_r_r = radius_world(hand_r, -1, 0.04f);

        // ~75kg total, limbs connected with d6 limits
        const int32_t hips_body = AddBoneBody(
            world_globals, hips_index, torso_child, hips_r, 14.0f, 0.35f, 25.0f, 0.12f, 0.75f
        );
        const int32_t torso_body = AddBoneBody(
            world_globals,
            torso_child,
            torso_end,
            torso_r,
            12.0f,
            0.35f,
            25.0f,
            0.12f,
            0.75f
        );
        const int32_t head_body = AddBoneBody(
            world_globals,
            head_joint,
            -1,
            head_r,
            4.0f,
            0.35f,
            25.0f,
            max(0.14f, head_r * 1.7f),
            max(0.22f, head_r * 2.4f)
        );

        // extremities: high ang damp + low max spin so hands settle
        constexpr float tip_ang_damp = 3.0f;
        constexpr float tip_max_ang = 6.0f;

        // thigh = hip to knee, calf = knee to ankle, foot = ankle along toes
        const int32_t thigh_l_body = AddBoneBody(
            world_globals, thigh_l, calf_l, thigh_r_l, 8.0f, 0.35f, 25.0f, 0.12f, 0.65f
        );
        const int32_t calf_l_body = AddBoneBody(
            world_globals,
            calf_l,
            foot_l,
            calf_r_l,
            5.0f,
            0.35f,
            25.0f,
            0.18f,
            0.55f
        );
        const int32_t foot_l_body = foot_l >= 0
            ? AddFootBody(world_globals, foot_l, ball_l, 2.0f)
            : -1;
        const int32_t thigh_r_body = AddBoneBody(
            world_globals, thigh_r, calf_r, thigh_r_r, 8.0f, 0.35f, 25.0f, 0.12f, 0.65f
        );
        const int32_t calf_r_body = AddBoneBody(
            world_globals,
            calf_r,
            foot_r,
            calf_r_r,
            5.0f,
            0.35f,
            25.0f,
            0.18f,
            0.55f
        );
        const int32_t foot_r_body = foot_r >= 0
            ? AddFootBody(world_globals, foot_r, ball_r, 2.0f)
            : -1;

        const int32_t arm_l_body = AddBoneBody(
            world_globals, arm_l, forearm_l, arm_r_l, 3.0f, 0.35f, 25.0f, 0.08f, 0.5f
        );
        const int32_t forearm_l_body = AddBoneBody(
            world_globals,
            forearm_l,
            hand_l >= 0 ? hand_l : -1,
            forearm_r_l,
            2.0f
        );
        const int32_t hand_l_body = hand_l >= 0
            ? AddBoneBody(
                world_globals, hand_l, -1, hand_r_l, 1.5f, tip_ang_damp, tip_max_ang,
                max(0.06f, hand_r_l * 1.5f), max(0.12f, hand_r_l * 2.2f)
            )
            : -1;
        const int32_t arm_r_body = AddBoneBody(
            world_globals, arm_r, forearm_r, arm_r_r, 3.0f, 0.35f, 25.0f, 0.08f, 0.5f
        );
        const int32_t forearm_r_body = AddBoneBody(
            world_globals,
            forearm_r,
            hand_r >= 0 ? hand_r : -1,
            forearm_r_r,
            2.0f
        );
        const int32_t hand_r_body = hand_r >= 0
            ? AddBoneBody(
                world_globals, hand_r, -1, hand_r_r, 1.5f, tip_ang_damp, tip_max_ang,
                max(0.06f, hand_r_r * 1.5f), max(0.12f, hand_r_r * 2.2f)
            )
            : -1;

        if (m_bodies.size() < 4)
        {
            DestroyRagdoll();
            return false;
        }

        AddBoneJoint(hips_body, torso_body, 0.65f, 0.65f, 0.3f);
        AddBoneJoint(torso_body, head_body, 0.5f, 0.5f, 0.35f);
        AddBoneJoint(hips_body, thigh_l_body, 1.0f, 0.6f, 0.3f);
        AddBoneJoint(thigh_l_body, calf_l_body, 0.12f, 1.25f, 0.12f);
        if (foot_l_body >= 0)
        {
            // tight ankle, mostly follows the calf
            AddBoneJoint(calf_l_body, foot_l_body, 0.1f, 0.22f, 0.06f);
        }
        AddBoneJoint(hips_body, thigh_r_body, 1.0f, 0.6f, 0.3f);
        AddBoneJoint(thigh_r_body, calf_r_body, 0.12f, 1.25f, 0.12f);
        if (foot_r_body >= 0)
        {
            AddBoneJoint(calf_r_body, foot_r_body, 0.1f, 0.22f, 0.06f);
        }
        AddBoneJoint(torso_body, arm_l_body, 1.15f, 1.15f, 0.7f);
        AddBoneJoint(arm_l_body, forearm_l_body, 0.12f, 1.35f, 0.2f);
        if (hand_l_body >= 0)
        {
            AddBoneJoint(forearm_l_body, hand_l_body, 0.25f, 0.3f, 0.15f);
        }
        AddBoneJoint(torso_body, arm_r_body, 1.15f, 1.15f, 0.7f);
        AddBoneJoint(arm_r_body, forearm_r_body, 0.12f, 1.35f, 0.2f);
        if (hand_r_body >= 0)
        {
            AddBoneJoint(forearm_r_body, hand_r_body, 0.25f, 0.3f, 0.15f);
        }

        // hit_velocity is already shaped/capped in ProcessHits, do not scale again
        const Vector3 launch = clamp_vector_length(hit_velocity, max_launch_speed);
        const Vector3 angular(launch.z * 0.08f, 0.0f, -launch.x * 0.08f);
        const bool have_hit = hit_position.LengthSquared() > 0.0001f;

        for (BoneBody& body : m_bodies)
        {
            if (!body.actor)
            {
                continue;
            }

            // car may overlap capsules on the activate frame, soft depenetration
            body.actor->setMaxDepenetrationVelocity(1.0f);
            body.actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);

            const PxTransform pose = body.actor->getGlobalPose();
            float weight = 1.0f;
            if (have_hit)
            {
                Vector3 to_body(
                    pose.p.x - hit_position.x,
                    pose.p.y - hit_position.y,
                    pose.p.z - hit_position.z
                );
                const float distance = to_body.Length();
                weight = distance < 0.01f
                    ? 1.0f
                    : clamp(1.0f / (1.0f + distance * 0.65f), 0.4f, 1.0f);
            }

            body.actor->setLinearVelocity(PxVec3(
                launch.x * weight,
                launch.y * weight,
                launch.z * weight
            ));

            // skip tip spin, light feet amplify it into endless flop
            const bool is_tip =
                body.joint_index == foot_l ||
                body.joint_index == foot_r ||
                body.joint_index == hand_l ||
                body.joint_index == hand_r;
            if (!is_tip)
            {
                body.actor->setAngularVelocity(PxVec3(
                    angular.x * weight,
                    angular.y * weight,
                    angular.z * weight
                ));
            }

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
                    continue;
                }

                local = model_globals[i] * parent_global.Inverted();
            }

            if (!IsFiniteMatrix(local) || matrix_looks_bad(local))
            {
                continue;
            }

            local = Matrix(local.GetTranslation(), local.GetRotation(), Vector3::One);
            m_pose_locals[i] = local;
        }

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

            if (!BuildRagdoll(*skeleton, m_pose_locals, hit_velocity, hit_position))
            {
                return false;
            }

            m_state = State::Simulating;
            m_sleep_timer = 0.0f;
            m_animator->SetExternalPose(m_pose_locals);
            UpdateCullBounds();
            return true;
        }

        // zero velocity = gentle wake for picking, do not invent a launch impulse
        const bool apply_impulse = hit_velocity.LengthSquared() >= 0.01f;
        Vector3 launch = Vector3::Zero;
        Vector3 angular = Vector3::Zero;
        if (apply_impulse)
        {
            launch = shape_launch_velocity(hit_velocity);
            angular = Vector3(launch.z * 0.06f, 0.0f, -launch.x * 0.06f);
        }

        {
            lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
            for (BoneBody& body : m_bodies)
            {
                if (!body.actor)
                {
                    continue;
                }

                body.actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, false);
                body.actor->setLinearVelocity(PxVec3(0.0f, 0.0f, 0.0f));
                body.actor->setAngularVelocity(PxVec3(0.0f, 0.0f, 0.0f));
                body.actor->clearForce();
                body.actor->clearTorque();
                // kinematic rest often overlaps the floor a bit, cap the pop on wake
                if (!apply_impulse)
                {
                    body.actor->setMaxDepenetrationVelocity(0.5f);
                }
                body.actor->wakeUp();
            }
        }

        // joints first while bodies are still, then optional hit impulse
        RecreateJoints();

        if (apply_impulse)
        {
            lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
            for (BoneBody& body : m_bodies)
            {
                if (!body.actor)
                {
                    continue;
                }

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
            }
        }
        else
        {
            // recreating joints can leave tiny residual motion, kill it for grab
            PrepareForPick();
        }

        m_state = State::Simulating;
        m_sleep_timer = 0.0f;
        UpdateCullBounds();
        return true;
    }

    void Ragdoll::PrepareForPick()
    {
        if (m_bodies.empty())
        {
            return;
        }

        lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
        for (BoneBody& body : m_bodies)
        {
            if (!body.actor)
            {
                continue;
            }

            body.actor->setLinearVelocity(PxVec3(0.0f, 0.0f, 0.0f));
            body.actor->setAngularVelocity(PxVec3(0.0f, 0.0f, 0.0f));
            body.actor->clearForce();
            body.actor->clearTorque();
            body.actor->setMaxDepenetrationVelocity(0.5f);
        }
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
