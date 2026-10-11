/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =======
#include "Vector3.h"
//==================

namespace spartan
{
    namespace math
    {
        class RayHit;
        class BoundingBox;

        class Ray
        {
        public:
            Ray() = default;
            Ray(const Vector3& start, const Vector3& direction);
            ~Ray() = default;

            // returns hit distance or infinity if there is no hit
            float HitDistance(const BoundingBox& box) const;
            float HitDistance(const Vector3& v1, const Vector3& v2, const Vector3& v3, Vector3* out_normal = nullptr, Vector3* out_bary = nullptr) const;

            const Vector3& GetStart()     const { return m_origin; }
            const Vector3& GetDirection() const { return m_direction; }

            Vector3 m_origin    = Vector3::Zero;
            Vector3 m_direction = Vector3::Zero;
        };
    }
}
