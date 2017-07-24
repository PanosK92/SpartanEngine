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

#pragma once

//= INCLUDES ==========
#include "MathHelper.h"
#include "Vector3.h"
#include <memory>
//=====================

namespace Directus
{
	class Mesh;
	namespace Math
	{
		class Matrix;

		class BoundingBox
		{
		public:
			BoundingBox();
			~BoundingBox();

			// Assign from bounding box
			BoundingBox& operator =(const BoundingBox& rhs)
			{
				min = rhs.min;
				max = rhs.max;
				return *this;
			}

			// Computes a bounding box from a mesh
			void ComputeFromMesh(std::weak_ptr<Mesh> mesh);
			void ComputeFromMesh(Mesh* mesh);

			// Returns the center
			Vector3 GetCenter() { return (min + max) * 0.5f; }

			// Returns the size
			Vector3 GetSize() { return max - min; }

			// Returns the half size (extents)
			Vector3 GetHalfSize() { return (max - min) * 0.5f; }

			// Test if a point is inside
			Intersection IsInside(const Vector3& point) const;

			// Test if a bounding box is inside
			Intersection IsInside (const BoundingBox& box) const;

			// Returns a transformed bounding box
			BoundingBox Transformed(const Matrix& matrix);

			// Merge with another bounding box
			void Merge(const BoundingBox& box);

			void Undefine() { min = Vector3::InfinityNeg; max = Vector3::Infinity; }
			bool Defined() const { return min.x != INFINITY; }

			Vector3 min;
			Vector3 max;
		};
	}
}
