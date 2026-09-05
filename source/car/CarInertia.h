#pragma once
#include "CarState.h"
#include <physx/foundation/PxMathUtils.h>
namespace car
{
    inline PxMat33 parallel_axis(float mass, const PxVec3& r)
    {
        return (PxMat33::createDiagonal(PxVec3(r.dot(r))) - PxMat33(r * r.x, r * r.y, r * r.z)) * mass;
    }
    inline PxMat33 actor_inertia_about(const PxRigidBody& actor, const PxTransform& frame, const PxVec3& origin)
    {
        PxTransform com = frame.getInverse() * actor.getGlobalPose() * actor.getCMassLocalPose();
        PxMat33 rotation(com.q);
        return rotation * PxMat33::createDiagonal(actor.getMassSpaceInertiaTensor()) * rotation.getTranspose() + parallel_axis(actor.getMass(), com.p - origin);
    }
    inline bool physical_inertia(const PxMat33& tensor, PxVec3& principal, PxQuat& axes)
    {
        principal = PxDiagonalize(tensor, axes);
        return principal.isFinite() && principal.minElement() > 0.01f
            && 2 * principal.maxElement() <= principal.x + principal.y + principal.z + 0.01f;
    }
}
