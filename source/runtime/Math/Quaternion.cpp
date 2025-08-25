/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ===
#include "pch.h"
//==============

//= NAMESPACES =====
using namespace std;
//==================

//= Based on ==========================================================================//
// http://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/index.htm //
// Heading  -> Yaw        -> Y-axis                                                    //
// Attitude -> Pitch    -> X-axis                                                      //
// Bank     -> Roll        -> Z-axis                                                   //
//=====================================================================================//

namespace spartan::math
{
    const Quaternion Quaternion::Identity(0.0f, 0.0f, 0.0f, 1.0f);

    void Quaternion::FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis)
    {
        *this = Matrix(
            xAxis.x, xAxis.y, xAxis.z, 0.0f,
            yAxis.x, yAxis.y, yAxis.z, 0.0f,
            zAxis.x, zAxis.y, zAxis.z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        ).GetRotation();
    }

    string Quaternion::ToString() const
    {
        char tempBuffer[200];
        sprintf_s(tempBuffer, sizeof(tempBuffer), "X:%f, Y:%f, Z:%f, W:%f", x, y, z, w);
        return string(tempBuffer);
    }
}
