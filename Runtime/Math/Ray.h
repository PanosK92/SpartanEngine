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

#pragma once

//= INCLUDES =======
#include "Vector3.h"
//==================

namespace Directus::Math
{
	class BoundingBox;

	class ENGINE_CLASS Ray
	{
	public:
		// Constructs a ray with zero origin and direction
		Ray();

		// Construct from origin and direction. The direction will be normalized.
		Ray(const Vector3& origin, const Vector3& end);

		// Empty destructor
		~Ray() {}

		// Returns hit distance to a bounding box, or infinity if there is no hit.
		float HitDistance(const BoundingBox& box);

		const Vector3& GetOrigin() const	{ return m_origin; }
		const Vector3& GetEnd()	const		{ return m_end; }
		const Vector3& GetDirection() const	{ return m_direction; }

	private:
		Vector3 m_origin;
		Vector3 m_end;
		Vector3 m_direction;
	};
}