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
        // compute quaternion directly from rotation matrix axes (avoids unstable GetRotation decomposition)
        // based on: http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
        const float m00 = xAxis.x, m01 = xAxis.y, m02 = xAxis.z;
        const float m10 = yAxis.x, m11 = yAxis.y, m12 = yAxis.z;
        const float m20 = zAxis.x, m21 = zAxis.y, m22 = zAxis.z;
        
        const float trace = m00 + m11 + m22;
        
        if (trace > 0.0f)
        {
            const float s = 0.5f / sqrtf(trace + 1.0f);
            w = 0.25f / s;
            x = (m12 - m21) * s;
            y = (m20 - m02) * s;
            z = (m01 - m10) * s;
        }
        else if (m00 > m11 && m00 > m22)
        {
            const float s = 2.0f * sqrtf(1.0f + m00 - m11 - m22);
            w = (m12 - m21) / s;
            x = 0.25f * s;
            y = (m10 + m01) / s;
            z = (m20 + m02) / s;
        }
        else if (m11 > m22)
        {
            const float s = 2.0f * sqrtf(1.0f + m11 - m00 - m22);
            w = (m20 - m02) / s;
            x = (m10 + m01) / s;
            y = 0.25f * s;
            z = (m21 + m12) / s;
        }
        else
        {
            const float s = 2.0f * sqrtf(1.0f + m22 - m00 - m11);
            w = (m01 - m10) / s;
            x = (m20 + m02) / s;
            y = (m21 + m12) / s;
            z = 0.25f * s;
        }
        
        // ensure canonical form (w >= 0)
        if (w < 0.0f)
        {
            x = -x;
            y = -y;
            z = -z;
            w = -w;
        }
    }

    string Quaternion::ToString() const
    {
        char tempBuffer[200];
        sprintf_s(tempBuffer, sizeof(tempBuffer), "X:%f, Y:%f, Z:%f, W:%f", x, y, z, w);
        return string(tempBuffer);
    }
}
