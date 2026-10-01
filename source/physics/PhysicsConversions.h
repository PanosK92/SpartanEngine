/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
namespace spartan::physics_detail
{
        // tag all shapes on an actor with a collision type in word2
        // used by the simulation filter shader to suppress specific pairs
        inline void tag_actor_shapes(PxRigidActor* actor, PxU32 collision_type, PxU32 collision_group = 0)
        {
            PxShape* shapes[16];
            PxU32 count = actor->getShapes(shapes, 16);
            for (PxU32 i = 0; i < count; i++)
            {
                PxFilterData fd = shapes[i]->getSimulationFilterData();
                fd.word2 = collision_type;
                fd.word3 = collision_group;
                shapes[i]->setSimulationFilterData(fd);
            }
        }

        // helper to build lock flags from position and rotation lock vectors
        inline PxRigidDynamicLockFlags build_lock_flags(const Vector3& position_lock, const Vector3& rotation_lock)
        {
            PxRigidDynamicLockFlags flags = PxRigidDynamicLockFlags(0);
            if (position_lock.x)
            {
                flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
            }
            if (position_lock.y)
            {
                flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
            }
            if (position_lock.z)
            {
                flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
            }
            if (rotation_lock.x)
            {
                flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
            }
            if (rotation_lock.y)
            {
                flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
            }
            if (rotation_lock.z)
            {
                flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
            }
            return flags;
        }

        // transform conversion helpers
        inline PxTransform to_px_transform(const Vector3& pos, const Quaternion& rot)
        {
            const Vector3 local = PhysicsWorld::ToPhysicsPosition(pos);
            return PxTransform(PxVec3(local.x, local.y, local.z), PxQuat(rot.x, rot.y, rot.z, rot.w));
        }

        inline PxTransform to_px_transform(const math::Matrix& matrix)
        {
            Vector3 pos = matrix.GetTranslation();
            Quaternion rot = matrix.GetRotation();
            return to_px_transform(pos, rot);
        }

        inline void from_px_transform(const PxTransform& pose, Vector3& pos, Quaternion& rot)
        {
            pos = PhysicsWorld::ToWorldPosition(Vector3(pose.p.x, pose.p.y, pose.p.z));
            rot = Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w);
        }

        inline Vector3 from_px_vec3(const PxVec3& v)
        {
            return Vector3(v.x, v.y, v.z);
        }

        inline PxVec3 to_px_vec3(const Vector3& v)
        {
            return PxVec3(v.x, v.y, v.z);
        }
}
