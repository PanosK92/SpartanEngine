/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =====
#include "Plane.h"
//================

namespace Directus::Math
{
	Plane::Plane()
	{
		d = 0;
	}

	Plane::Plane(const Vector3& normal, float d)
	{
		this->normal = normal;
		this->d = d;
	}

	Plane::Plane(const Vector3& a, const Vector3& b, const Vector3& c)
	{
		Vector3 ab = b - a;
		Vector3 ac = c - a;

		Vector3 cross = Vector3::Cross(ab, ac);
		this->normal = Vector3::Normalize(cross);
		this->d = -Vector3::Dot(normal, a);
	}

	Plane::~Plane()
	{
	}

	void Plane::Normalize()
	{
		Plane result;

		result.normal = Vector3::Normalize(this->normal);
		float nominator = sqrtf(result.normal.x * result.normal.x + result.normal.y * result.normal.y + result.normal.z * result.normal.z);
		float denominator = sqrtf(this->normal.x * this->normal.x + this->normal.y * this->normal.y + this->normal.z * this->normal.z);
		float fentity = nominator / denominator;
		result.d = this->d * fentity;

		this->normal = result.normal;
		this->d = result.d;
	}

	Plane Plane::Normalize(const Plane& plane)
	{
		Plane newPlane = plane;
		newPlane.Normalize();
		return newPlane;
	}

	float Plane::DotCoordinate(const Vector3& v)
	{
		return (this->normal.x * v.x) + (this->normal.y * v.y) + (this->normal.z * v.z) + this->d;
	}

	float Plane::DotCoordinate(const Plane& p, const Vector3& v)
	{
		Plane newPlane = p;
		return newPlane.DotCoordinate(v);
	}
}
