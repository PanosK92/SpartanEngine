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
