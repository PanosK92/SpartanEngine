/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
