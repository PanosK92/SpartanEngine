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
#include "Ray.h"
//==================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan::Math
{
    Ray::Ray(const Vector3& start, const Vector3& end)
    {
        m_start                     = start;
        m_end                       = end;
        const Vector3 start_to_end  = (end - start);
        m_length                    = start_to_end.Length();
        m_direction                 = start_to_end.Normalized();
    }

    float Ray::HitDistance(const BoundingBox& box) const
    {
        // If undefined, no hit (infinite distance)
        if (!box.Defined())
            return INFINITY;
        
        // Check for ray origin being inside the box
        if (box.IsInside(m_start))
            return 0.0f;

        auto dist = INFINITY;

        // Check for intersecting in the X-direction
        if (m_start.x < box.GetMin().x && m_direction.x > 0.0f)
        {
            const auto x = (box.GetMin().x - m_start.x) / m_direction.x;
            if (x < dist)
            {
                const auto point = m_start + x * m_direction;
                if (point.y >= box.GetMin().y && point.y <= box.GetMax().y && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
                {
                    dist = x;
                }
            }
        }
        if (m_start.x > box.GetMax().x && m_direction.x < 0.0f)
        {
            const auto x = (box.GetMax().x - m_start.x) / m_direction.x;
            if (x < dist)
            {
                const auto point = m_start + x * m_direction;
                if (point.y >= box.GetMin().y && point.y <= box.GetMax().y && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
                {
                    dist = x;
                }
            }
        }

        // Check for intersecting in the Y-direction
        if (m_start.y < box.GetMin().y && m_direction.y > 0.0f)
        {
            const auto x = (box.GetMin().y - m_start.y) / m_direction.y;
            if (x < dist)
            {
                const auto point = m_start + x * m_direction;
                if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
                {
                    dist = x;
                }
            }
        }
        if (m_start.y > box.GetMax().y && m_direction.y < 0.0f)
        {
            const auto x = (box.GetMax().y - m_start.y) / m_direction.y;
            if (x < dist)
            {
                const auto point = m_start + x * m_direction;
                if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
                {
                    dist = x;
                }
            }
        }

        // Check for intersecting in the Z-direction
        if (m_start.z < box.GetMin().z && m_direction.z > 0.0f)
        {
            const auto x = (box.GetMin().z - m_start.z) / m_direction.z;
            if (x < dist)
            {
                const auto point = m_start + x * m_direction;
                if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.y >= box.GetMin().y && point.y <= box.GetMax().y)
                {
                    dist = x;
                }
            }
        }
        if (m_start.z > box.GetMax().z && m_direction.z < 0.0f)
        {
            const auto x = (box.GetMax().z - m_start.z) / m_direction.z;
            if (x < dist)
            {
                const auto point = m_start + x * m_direction;
                if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.y >= box.GetMin().y && point.y <= box.GetMax().y)
                {
                    dist = x;
                }
            }
        }

        return dist;
    }

    float Ray::HitDistance(const Vector3& v1, const Vector3& v2, const Vector3& v3, Vector3* out_normal /*= nullptr*/, Vector3* out_bary /*= nullptr*/) const
    {
        // Based on Fast, Minimum Storage Ray/Triangle Intersection by Möller & Trumbore
        // http://www.graphics.cornell.edu/pubs/1997/MT97.pdf
        // Calculate edge vectors
        Vector3 edge1(v2 - v1);
        Vector3 edge2(v3 - v1);

        // Calculate determinant & check backfacing
        Vector3 p(m_direction.Cross(edge2));
        float det = edge1.Dot(p);

        if (det >= Helper::EPSILON)
        {
            // Calculate u & v parameters and test
            Vector3 t(m_start - v1);
            float u = t.Dot(p);
            if (u >= 0.0f && u <= det)
            {
                Vector3 q(t.Cross(edge1));
                float v = m_direction.Dot(q);
                if (v >= 0.0f && u + v <= det)
                {
                    float distance = edge2.Dot(q) / det;

                    // Discard hits behind the ray
                    if (distance >= 0.0f)
                    {
                        // There is an intersection, so calculate distance & optional normal
                        if (out_normal)
                            *out_normal = edge1.Cross(edge2);
                        if (out_bary)
                            *out_bary = Vector3(1 - (u / det) - (v / det), u / det, v / det);

                        return distance;
                    }
                }
            }
        }

        return Helper::INFINITY_;
    }  
}
