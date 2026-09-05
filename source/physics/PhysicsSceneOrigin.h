#pragma once
#include <physx/PxScene.h>
#include <cmath>

namespace spartan
{
    // PhysX integrates TGS poses once per position iteration. At kilometre
    // coordinates, those small increments round away even at driving speeds.
    // Keep the active area local, shifting only when it crosses a 64 m boundary.
    // Elevation stays in world space; all translations here are horizontal.
    class PhysicsSceneOrigin
    {
    public:
        physx::PxVec3 offset = physx::PxVec3(0);

        physx::PxVec3 Update(physx::PxScene& scene, const physx::PxVec3& world_focus)
        {
            using namespace physx;
            const PxVec3 local = world_focus - offset;
            if (!local.isFinite() || (fabsf(local.x) <= 64 && fabsf(local.z) <= 64)) return PxVec3(0);
            const PxVec3 shift(64 * std::round(local.x / 64), 0, 64 * std::round(local.z / 64));
            scene.shiftOrigin(shift);
            offset += shift;
            return shift;
        }
    };
}
