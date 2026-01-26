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

//= INCLUDES =================
#include "pch.h"
#include "../RHI/RHI_Vertex.h"
//============================

namespace spartan::math
{
    const BoundingBox BoundingBox::Zero(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f));
    const BoundingBox BoundingBox::Unit(Vector3(-0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, 0.5f));
    const BoundingBox BoundingBox::Infinite(Vector3::InfinityNeg, Vector3::Infinity);

    BoundingBox::BoundingBox()
    {
        m_min = Vector3::Infinity;
        m_max = Vector3::InfinityNeg;
    }

    BoundingBox::BoundingBox(const Vector3& min, const Vector3& max)
    {
        this->m_min = min;
        this->m_max = max;
    }

    BoundingBox::BoundingBox(const Vector3* points, const uint32_t point_count)
    {
        m_min = Vector3::Infinity;
        m_max = Vector3::InfinityNeg;

        for (uint32_t i = 0; i < point_count; i++)
        {
            m_max.x = std::max(m_max.x, points[i].x);
            m_max.y = std::max(m_max.y, points[i].y);
            m_max.z = std::max(m_max.z, points[i].z);

            m_min.x = std::min(m_min.x, points[i].x);
            m_min.y = std::min(m_min.y, points[i].y);
            m_min.z = std::min(m_min.z, points[i].z);
        }
    }

    BoundingBox::BoundingBox(const RHI_Vertex_PosTexNorTan* vertices, const uint32_t vertex_count)
    {
        m_min = Vector3::Infinity;
        m_max = Vector3::InfinityNeg;

        for (uint32_t i = 0; i < vertex_count; i++)
        {
            m_max.x = std::max(m_max.x, vertices[i].pos[0]);
            m_max.y = std::max(m_max.y, vertices[i].pos[1]);
            m_max.z = std::max(m_max.z, vertices[i].pos[2]);

            m_min.x = std::min(m_min.x, vertices[i].pos[0]);
            m_min.y = std::min(m_min.y, vertices[i].pos[1]);
            m_min.z = std::min(m_min.z, vertices[i].pos[2]);
        }
    }

    Intersection BoundingBox::Intersects(const Vector3& point) const
    {
        if (point.x < m_min.x || point.x > m_max.x ||
            point.y < m_min.y || point.y > m_max.y ||
            point.z < m_min.z || point.z > m_max.z)
        {
            return Intersection::Outside;
        }

        return Intersection::Inside;
    }

    Intersection BoundingBox::Intersects(const BoundingBox& box) const
    {
        if (box.m_max.x < m_min.x || box.m_min.x > m_max.x ||
            box.m_max.y < m_min.y || box.m_min.y > m_max.y ||
            box.m_max.z < m_min.z || box.m_min.z > m_max.z)
        {
            return Intersection::Outside;
        }
        else if (
                box.m_min.x < m_min.x || box.m_max.x > m_max.x ||
                box.m_min.y < m_min.y || box.m_max.y > m_max.y ||
                box.m_min.z < m_min.z || box.m_max.z > m_max.z)
        {
            return Intersection::Intersects;
        }

        return Intersection::Inside;
    }

    void BoundingBox::Merge(const BoundingBox& box)
    {
        m_min.x = std::min(m_min.x, box.m_min.x);
        m_min.y = std::min(m_min.y, box.m_min.y);
        m_min.z = std::min(m_min.z, box.m_min.z);

        m_max.x = std::max(m_max.x, box.m_max.x);
        m_max.y = std::max(m_max.y, box.m_max.y);
        m_max.z = std::max(m_max.z, box.m_max.z);
    }

    Vector3 BoundingBox::GetClosestPoint(const Vector3& point) const
    {
        return Vector3(
            std::max(m_min.x, std::min(point.x, m_max.x)),
            std::max(m_min.y, std::min(point.y, m_max.y)),
            std::max(m_min.z, std::min(point.z, m_max.z))
        );
    }

    bool BoundingBox::Contains(const Vector3& point) const
    {
        return (point.x >= m_min.x && point.x <= m_max.x) &&
               (point.y >= m_min.y && point.y <= m_max.y) &&
               (point.z >= m_min.z && point.z <= m_max.z);
    }
}
