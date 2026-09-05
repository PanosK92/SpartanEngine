#pragma once
#include "CarMultibody.h"

namespace car
{
    // Keep the two anchors separate: substituting a link endpoint for both
    // sides conceals constraint error in the skeleton view.
    inline bool joint_world_frames(const PxJoint* joint, PxTransform& a, PxTransform& b)
    {
        if (!joint) return false;
        PxRigidActor* actor_a = nullptr;
        PxRigidActor* actor_b = nullptr;
        joint->getActors(actor_a, actor_b);
        a = (actor_a ? actor_a->getGlobalPose() : PxTransform(PxIdentity)) * joint->getLocalPose(PxJointActorIndex::eACTOR0);
        b = (actor_b ? actor_b->getGlobalPose() : PxTransform(PxIdentity)) * joint->getLocalPose(PxJointActorIndex::eACTOR1);
        return a.isValid() && b.isValid();
    }
}
