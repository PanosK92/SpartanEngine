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

#pragma once

//= INCLUDES ==========
#include "MathHelper.h"
#include "Vector3.h"
//=====================

namespace Spartan
{
    class Mesh;
    struct RHI_Vertex_PosTexNorTan;

    namespace Math
    {
        class Matrix;

        class SPARTAN_CLASS BoundingBox
        {
        public:
            // Construct with zero size.
            BoundingBox();

            // Construct from minimum and maximum vectors.
            BoundingBox(const Vector3& min, const Vector3& max);

            // Construct from points
            BoundingBox(const Vector3* vertices, const uint32_t point_count);

            // Construct from vertices
            BoundingBox(const RHI_Vertex_PosTexNorTan* vertices, const uint32_t vertex_count);

            ~BoundingBox() = default;

            // Assign from bounding box
            BoundingBox& operator =(const BoundingBox& rhs) = default;

            // Returns the center
            Vector3 GetCenter() const   { return (m_max + m_min) * 0.5f; }

            // Returns the size
            Vector3 GetSize() const     { return m_max - m_min; }

            // Returns extents
            Vector3 GetExtents() const  { return (m_max - m_min) * 0.5f; }

            // Test if a point is inside
            Intersection IsInside(const Vector3& point) const;

            // Test if a bounding box is inside
            Intersection IsInside (const BoundingBox& box) const;

            // Returns a transformed bounding box
            BoundingBox Transform(const Matrix& transform) const;

            // Merge with another bounding box
            void Merge(const BoundingBox& box);

            const Vector3& GetMin() const { return m_min; }
            const Vector3& GetMax() const { return m_max; }

            void Undefine()            { m_min = Vector3::InfinityNeg; m_max = Vector3::Infinity; }
            bool Defined() const    { return m_min.x != INFINITY; }

            static const BoundingBox Zero;

        private:
            Vector3 m_min;
            Vector3 m_max;    
        };
    }
}
