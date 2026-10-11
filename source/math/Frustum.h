/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =============
#include "Matrix.h"
#include "Vector3.h"
//========================

namespace spartan::math
{
    class Frustum
    {
    public:
        Frustum() = default;
        Frustum(const Matrix& view, const Matrix& projection);
        ~Frustum() = default;

        bool IsVisible(const Vector3& center, const Vector3& extent, bool ignore_depth = false) const;

    private:
        // one simd lane per plane (near, far, left, right, top, bottom), the last two lanes are zero and masked out
        float m_plane_x[8] = {};
        float m_plane_y[8] = {};
        float m_plane_z[8] = {};
        float m_plane_d[8] = {};
    };
}
