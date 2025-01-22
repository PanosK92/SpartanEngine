/*
Copyright(c) 2016-2025 Panos Karabelas

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
#include "pch.h"
//==================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::math
{
    Ray::Ray(const Vector3& start, const Vector3& direction)
    {
        m_origin    = start;
        m_direction = direction.Normalized();
    }

    float Ray::HitDistance(const BoundingBox& box) const
    {
        // If undefined, no hit (infinite distance)
        if (box == BoundingBox::Undefined)
            return INFINITY_;
        
        // check for ray origin being inside the box
        if (box.Intersects(m_origin) == Intersection::Inside)
            return 0.0f;

        float distance = INFINITY_;

        // Check for intersecting in the X-direction
        if (m_origin.x < box.GetMin().x && m_direction.x > 0.0f)
        {
            const float x = (box.GetMin().x - m_origin.x) / m_direction.x;
            if (x < distance)
            {
                const Vector3 point = m_origin + x * m_direction;
                if (point.y >= box.GetMin().y && point.y <= box.GetMax().y && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
                {
                    distance = x;
                }
            }
        }
        if (m_origin.x > box.GetMax().x && m_direction.x < 0.0f)
        {
            const float x = (box.GetMax().x - m_origin.x) / m_direction.x;
            if (x < distance)
            {
                const Vector3 point = m_origin + x * m_direction;
                if (point.y >= box.GetMin().y && point.y <= box.GetMax().y && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
                {
                    distance = x;
                }
            }
        }

        // Check for intersecting in the Y-direction
        if (m_origin.y < box.GetMin().y && m_direction.y > 0.0f)
        {
            const float x = (box.GetMin().y - m_origin.y) / m_direction.y;
            if (x < distance)
            {
                const Vector3 point = m_origin + x * m_direction;
                if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
                {
                    distance = x;
                }
            }
        }
        if (m_origin.y > box.GetMax().y && m_direction.y < 0.0f)
        {
            const float x = (box.GetMax().y - m_origin.y) / m_direction.y;
            if (x < distance)
            {
                const Vector3 point = m_origin + x * m_direction;
                if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
                {
                    distance = x;
                }
            }
        }

        // Check for intersecting in the Z-direction
        if (m_origin.z < box.GetMin().z && m_direction.z > 0.0f)
        {
            const float x = (box.GetMin().z - m_origin.z) / m_direction.z;
            if (x < distance)
            {
                const Vector3 point = m_origin + x * m_direction;
                if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.y >= box.GetMin().y && point.y <= box.GetMax().y)
                {
                    distance = x;
                }
            }
        }
        if (m_origin.z > box.GetMax().z && m_direction.z < 0.0f)
        {
            const float x = (box.GetMax().z - m_origin.z) / m_direction.z;
            if (x < distance)
            {
                const Vector3 point = m_origin + x * m_direction;
                if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.y >= box.GetMin().y && point.y <= box.GetMax().y)
                {
                    distance = x;
                }
            }
        }

        return distance;
    }

    float Ray::HitDistance(const Plane& plane, Vector3* intersection_point /*= nullptr*/) const
    {
        float d = plane.normal.Dot(m_direction);
        if (abs(d) >= small_float)
        {
            float t = -(plane.normal.Dot(m_origin) + plane.d) / d;
            if (t >= 0.0f)
            {
                if (intersection_point)
                {
                    *intersection_point = m_origin + t * m_direction;
                }
                return t;
            }
            else
            {
                return INFINITY_;
            }
        }
        else
        {
            return INFINITY_;
        }
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

        if (det >= small_float)
        {
            // Calculate u & v parameters and test
            Vector3 t(m_origin - v1);
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

        return INFINITY_;
    }

    float Ray::HitDistance(const Sphere& sphere) const
    {
        Vector3 centeredOrigin = m_origin - sphere.center;
        float squaredRadius = sphere.radius * sphere.radius;

        // Check if ray originates inside the sphere
        if (centeredOrigin.LengthSquared() <= squaredRadius)
            return 0.0f;

        // Calculate intersection by quadratic equation
        float a = m_direction.Dot(m_direction);
        float b = 2.0f * centeredOrigin.Dot(m_direction);
        float c = centeredOrigin.Dot(centeredOrigin) - squaredRadius;
        float d = b * b - 4.0f * a * c;
    
        // No solution
        if (d < 0.0f)
            return INFINITY_;

        // Get the nearer solution
        float dSqrt = sqrtf(d);
        float dist = (-b - dSqrt) / (2.0f * a);
        if (dist >= 0.0f)
            return dist;
        else
            return (-b + dSqrt) / (2.0f * a);
    }

    float Ray::Distance(const Vector3& point) const
    {
        const Vector3 closest_point = m_origin + (m_direction * (point - m_origin).Dot(m_direction));
        return (closest_point - point).Length();
    }

    float Ray::Distance(const Vector3& point, Vector3& closest_point) const
    {
        closest_point = m_origin + (m_direction * (point - m_origin).Dot(m_direction));
        return (closest_point - point).Length();
    }

    Vector3 Ray::ClosestPoint(const Ray& ray) const
    {
        // Algorithm based on http://paulbourke.net/geometry/lineline3d/
        Vector3 p13 = m_origin - ray.m_origin;
        Vector3 p43 = ray.m_direction;
        Vector3 p21 = m_direction;
        
        float d1343 = p13.Dot(p43);
        float d4321 = p43.Dot(p21);
        float d1321 = p13.Dot(p21);
        float d4343 = p43.Dot(p43);
        float d2121 = p21.Dot(p21);
        
        float d = d2121 * d4343 - d4321 * d4321;
        if (abs(d) < small_float)
            return m_origin;

        float n = d1343 * d4321 - d1321 * d4343;
        float a = n / d;
        
        return m_origin + a * m_direction;
    }
}
