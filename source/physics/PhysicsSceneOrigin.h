/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include <physx/PxScene.h>
#include <cmath>

namespace spartan
{
    // PhysX integrates TGS poses once per position iteration. At kilometre
    // coordinates, those small increments round away even at driving speeds.
    // Keep the active area local, shifting when it crosses a 16 m boundary.
    // Elevation stays in world space; all translations here are horizontal.
    class PhysicsSceneOrigin
    {
    public:
        physx::PxVec3 offset = physx::PxVec3(0);

        physx::PxVec3 Update(physx::PxScene& scene, const physx::PxVec3& world_focus)
        {
            using namespace physx;
            const PxVec3 local = world_focus - offset;
            if (!local.isFinite() || (fabsf(local.x) <= 16 && fabsf(local.z) <= 16)) return PxVec3(0);
            // Center the focus itself. Grid snapping left up to 32 m of residual
            // coordinates even immediately after a rebase, enough to perturb
            // the small TGS suspension/steering increments at high tick rates.
            const PxVec3 shift(local.x, 0, local.z);
            scene.shiftOrigin(shift);
            offset += shift;
            return shift;
        }
    };
}
