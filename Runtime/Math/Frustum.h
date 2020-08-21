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

//= INCLUDES =============
#include "../Math/Plane.h"
#include "Matrix.h"
#include "Vector3.h"
//========================

namespace Spartan::Math
{
    class Frustum
    {
    public:
        Frustum() = default;
        Frustum(const Matrix& mView, const Matrix& mProjection, float screenDepth);
        ~Frustum() = default;

        bool IsVisible(const Vector3& center, const Vector3& extent, bool ignore_near_plane = false) const;

    private:
        Intersection CheckCube(const Vector3& center, const Vector3& extent) const;
        Intersection CheckSphere(const Vector3& center, float radius) const;

        Plane m_planes[6];
    };
}
