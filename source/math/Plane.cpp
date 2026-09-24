/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =======
#include "pch.h"
//==================

namespace spartan::math
{
    Plane::Plane(const Vector3& normal, float d)
    {
        this->normal = normal;
        this->d      = d;
    }

    Plane::Plane(const Vector3& a, const Vector3& b, const Vector3& c)
    {
        const Vector3 ab = b - a;
        const Vector3 ac = c - a;

        const Vector3 cross = Vector3::Cross(ab, ac);
        this->normal        = Vector3::Normalize(cross);
        this->d             = -Vector3::Dot(normal, a);
    }

    Plane::Plane(const Vector3& normal, const Vector3& point)
    {
        this->normal = normal.Normalized();
        d = -this->normal.Dot(point);
    }

    void Plane::Normalize()
    {
        Plane result;

        result.normal = Vector3::Normalize(this->normal);
        const float nominator = sqrtf(result.normal.x * result.normal.x + result.normal.y * result.normal.y + result.normal.z * result.normal.z);
        const float denominator = sqrtf(this->normal.x * this->normal.x + this->normal.y * this->normal.y + this->normal.z * this->normal.z);
        const float fentity = nominator / denominator;
        result.d = this->d * fentity;

        this->normal = result.normal;
        this->d = result.d;
    }

    Plane Plane::Normalize(const Plane& plane)
    {
        Plane normalized = plane;
        normalized.Normalize();
        return normalized;
    }

    float Plane::Dot(const Vector3& v) const
    {
        return (this->normal.x * v.x) + (this->normal.y * v.y) + (this->normal.z * v.z) + this->d;
    }

    float Plane::Dot(const Plane& plane, const Vector3& v)
    {
        return plane.Dot(v);
    }
}
