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
