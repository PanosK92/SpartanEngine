/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ================
#include "BoundingBox.h"
#include "../Graphics/Mesh.h"
#include "MathHelper.h"
#include "Matrix.h"
//===========================

namespace Directus
{
	namespace Math
	{
		BoundingBox::BoundingBox()
		{
			min = Vector3::Infinity;
			max = Vector3::InfinityNeg;
		}

		BoundingBox::BoundingBox(const Vector3& min, const Vector3& max)
		{
			this->min = min;
			this->max = max;
		}

		BoundingBox::~BoundingBox()
		{

		}

		void BoundingBox::ComputeFromMesh(std::weak_ptr<Mesh> mesh)
		{
			min = Vector3::Infinity;
			max = Vector3::InfinityNeg;

			if (mesh.expired())
				return;

			ComputeFromMesh(mesh._Get());
		}

		void BoundingBox::ComputeFromMesh(Mesh* mesh)
		{
			min = Vector3::Infinity;
			max = Vector3::InfinityNeg;

			for (unsigned int i = 0; i < mesh->GetVertexCount(); i++)
			{
				auto vertices = mesh->GetVertices();

				max.x = Max(max.x, vertices[i].position.x);
				max.y = Max(max.y, vertices[i].position.y);
				max.z = Max(max.z, vertices[i].position.z);

				min.x = Min(min.x, vertices[i].position.x);
				min.y = Min(min.y, vertices[i].position.y);
				min.z = Min(min.z, vertices[i].position.z);
			}
		}

		Intersection BoundingBox::IsInside(const Vector3& point) const
		{
			if
				(
					point.x < min.x || point.x > max.x ||
					point.y < min.y || point.y > max.y ||
					point.z < min.z || point.z > max.z
					)
			{
				return Outside;
			}
			else
			{
				return Inside;
			}
		}

		Intersection BoundingBox::IsInside(const BoundingBox& box) const
		{
			if
				(
					box.max.x < min.x || box.min.x > max.x ||
					box.max.y < min.y || box.min.y > max.y ||
					box.max.z < min.z || box.min.z > max.z)
			{
				return Outside;
			}
			else if
				(
					box.min.x < min.x || box.max.x > max.x ||
					box.min.y < min.y || box.max.y > max.y ||
					box.min.z < min.z || box.max.z > max.z)
			{
				return Intersects;
			}
			else
			{
				return Inside;
			}
		}

		BoundingBox BoundingBox::Transformed(const Matrix& transform)
		{
			Vector3 newCenter = transform * GetCenter();
			Vector3 oldEdge = GetSize() * 0.5f;
			Vector3 newEdge = Vector3
			(
				Abs(transform.m00) * oldEdge.x + Abs(transform.m10) * oldEdge.y + Abs(transform.m20) * oldEdge.z,
				Abs(transform.m01) * oldEdge.x + Abs(transform.m11) * oldEdge.y + Abs(transform.m21) * oldEdge.z,
				Abs(transform.m02) * oldEdge.x + Abs(transform.m12) * oldEdge.y + Abs(transform.m22) * oldEdge.z
			);

			return BoundingBox(newCenter - newEdge, newCenter + newEdge);
		}

		void BoundingBox::Merge(const BoundingBox& box)
		{
			if (box.min.x < min.x) min.x = box.min.x;
			if (box.min.y < min.y) min.y = box.min.y;
			if (box.min.z < min.z) min.z = box.min.z;
			if (box.max.x > max.x) max.x = box.max.x;
			if (box.max.y > max.y) max.y = box.max.y;
			if (box.max.z > max.z) max.z = box.max.z;
		}
	}
}