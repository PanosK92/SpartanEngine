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

//= INCLUDES ======
#include "pch.h"
#include "Instance.h"
#include <bit>
//=================

//= NAMESPACES ===============
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        uint32_t pack_rotation(float a, float b)
        {
            auto pack = [](float value)
            {
                return static_cast<uint16_t>(static_cast<int16_t>(std::round(std::clamp(value, -1.0f, 1.0f) * 32767.0f)));
            };
            return static_cast<uint32_t>(pack(a)) | (static_cast<uint32_t>(pack(b)) << 16);
        }

        float unpack_rotation(uint32_t value)
        {
            return static_cast<float>(std::bit_cast<int16_t>(static_cast<uint16_t>(value))) / 32767.0f;
        }
    }

    Matrix Instance::GetMatrix() const
    {
        Quaternion rotation(unpack_rotation(rotation_xy), unpack_rotation(rotation_xy >> 16),
            unpack_rotation(rotation_zw), unpack_rotation(rotation_zw >> 16));
        rotation.Normalize();
        return Matrix::CreateScale(Vector3(scale_x, scale_y, scale_z)) *
            Matrix::CreateRotation(rotation) * Matrix::CreateTranslation(Vector3(position_x, position_y, position_z));
    }

    void Instance::SetMatrix(const Matrix& matrix)
    {
        const Vector3 position = matrix.GetTranslation();
        position_x = position.x;
        position_y = position.y;
        position_z = position.z;
        const Quaternion rotation = matrix.GetRotation().Normalized();
        rotation_xy = pack_rotation(rotation.x, rotation.y);
        rotation_zw = pack_rotation(rotation.z, rotation.w);
        const Vector3 scale = matrix.GetScale();
        scale_x = scale.x;
        scale_y = scale.y;
        scale_z = scale.z;
    }

    Instance Instance::GetIdentity()
    {
        Instance instance;
        instance.SetMatrix(Matrix::Identity);
        return instance;
    }
}
