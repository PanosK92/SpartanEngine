/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES =======
#include "Spartan.h"
//==================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan::Math
{
    Frustum::Frustum(const Matrix& view, const Matrix& projection, float screen_depth)
    {
        // Calculate the minimum Z distance in the frustum.
        const float z_min           = -projection.m32 / projection.m22;
        const float r               = screen_depth / (screen_depth - z_min);
        Matrix projection_updated   = projection;
        projection_updated.m22      = r;
        projection_updated.m32      = -r * z_min;

        // Create the frustum matrix from the view matrix and updated projection matrix.
        const Matrix view_projection = view * projection_updated;

        // Calculate near plane of frustum.
        m_planes[0].normal.x = view_projection.m03 + view_projection.m02;
        m_planes[0].normal.y = view_projection.m13 + view_projection.m12;
        m_planes[0].normal.z = view_projection.m23 + view_projection.m22;
        m_planes[0].d = view_projection.m33 + view_projection.m32;
        m_planes[0].Normalize();

        // Calculate far plane of frustum.
        m_planes[1].normal.x = view_projection.m03 - view_projection.m02;
        m_planes[1].normal.y = view_projection.m13 - view_projection.m12;
        m_planes[1].normal.z = view_projection.m23 - view_projection.m22;
        m_planes[1].d = view_projection.m33 - view_projection.m32;
        m_planes[1].Normalize();

        // Calculate left plane of frustum.
        m_planes[2].normal.x = view_projection.m03 + view_projection.m00;
        m_planes[2].normal.y = view_projection.m13 + view_projection.m10;
        m_planes[2].normal.z = view_projection.m23 + view_projection.m20;
        m_planes[2].d = view_projection.m33 + view_projection.m30;
        m_planes[2].Normalize();

        // Calculate right plane of frustum.
        m_planes[3].normal.x = view_projection.m03 - view_projection.m00;
        m_planes[3].normal.y = view_projection.m13 - view_projection.m10;
        m_planes[3].normal.z = view_projection.m23 - view_projection.m20;
        m_planes[3].d = view_projection.m33 - view_projection.m30;
        m_planes[3].Normalize();

        // Calculate top plane of frustum.
        m_planes[4].normal.x = view_projection.m03 - view_projection.m01;
        m_planes[4].normal.y = view_projection.m13 - view_projection.m11;
        m_planes[4].normal.z = view_projection.m23 - view_projection.m21;
        m_planes[4].d = view_projection.m33 - view_projection.m31;
        m_planes[4].Normalize();

        // Calculate bottom plane of frustum.
        m_planes[5].normal.x = view_projection.m03 + view_projection.m01;
        m_planes[5].normal.y = view_projection.m13 + view_projection.m11;
        m_planes[5].normal.z = view_projection.m23 + view_projection.m21;
        m_planes[5].d = view_projection.m33 + view_projection.m31;
        m_planes[5].Normalize();
    }

    bool Frustum::IsVisible(const Vector3& center, const Vector3& extent, bool ignore_near_plane /*= false*/) const
    {
        float radius = 0.0f;

        if (!ignore_near_plane)
        {
            radius = Helper::Max3(extent.x, extent.y, extent.z);
        }
        else
        {
            constexpr float z = numeric_limits<float>::infinity(); // reverse-z only (but I must read form Renderer)
            radius = Helper::Max3(extent.x, extent.y, z);
        }

        // Check sphere first as it's cheaper
        if (CheckSphere(center, radius) != Outside)
            return true;

        if (CheckCube(center, radius) != Outside)
            return true;

        return false;
    }

    Intersection Frustum::CheckCube(const Vector3& center, const Vector3& extent) const
    {
        Intersection result = Inside;
        Plane plane_abs;

        // Check if any one point of the cube is in the view frustum.
        
        for (const Plane& plane : m_planes)
        {
            plane_abs.normal    = plane.normal.Abs();
            plane_abs.d         = plane.d;

            const float d   = center.x * plane.normal.x + center.y * plane.normal.y + center.z * plane.normal.z;
            const float r   = extent.x * plane_abs.normal.x + extent.y * plane_abs.normal.y + extent.z * plane_abs.normal.z;

            const float d_p_r = d + r;
            const float d_m_r = d - r;

            if (d_p_r < -plane.d)
            {
                result = Outside;
                break;
            }

            if (d_m_r < -plane.d)
            {
                result = Intersects;
            }
        }

        return result;
    }

    Intersection Frustum::CheckSphere(const Vector3& center, float radius) const
    {
        // calculate our distances to each of the planes
        for (const auto& plane : m_planes)
        {
            // find the distance to this plane
            const float distance = Vector3::Dot(plane.normal, center) + plane.d;

            // if this distance is < -sphere.radius, we are outside
            if (distance < -radius)
                return Outside;

            // else if the distance is between +- radius, then we intersect
            if (static_cast<float>(Helper::Abs(distance)) < radius)
                return Intersects;
        }

        // otherwise we are fully in view
        return Inside;
    }
}
