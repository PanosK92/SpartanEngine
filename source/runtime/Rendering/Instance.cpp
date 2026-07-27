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
        uint16_t encode_octahedral(const Vector3& direction)
        {
            Vector3 oct = direction / (std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z));
            if (oct.z < 0.0f)
            {
                float temp_x = oct.x;
                oct.x        = (1.0f - std::abs(oct.y)) * (temp_x >= 0.0f ? 1.0f : -1.0f);
                oct.y        = (1.0f - std::abs(temp_x)) * (oct.y >= 0.0f ? 1.0f : -1.0f);
            }

            uint8_t x = static_cast<uint8_t>(std::round((oct.x * 0.5f + 0.5f) * 255.0f));
            uint8_t y = static_cast<uint8_t>(std::round((oct.y * 0.5f + 0.5f) * 255.0f));

            return (static_cast<uint16_t>(x) << 8) | y;
        }

        Vector3 decode_octahedral(uint16_t packed)
        {
            float x = (static_cast<float>(packed >> 8) / 255.0f) * 2.0f - 1.0f;
            float y = (static_cast<float>(packed & 0xFF) / 255.0f) * 2.0f - 1.0f;
            float z = 1.0f - std::abs(x) - std::abs(y);
            if (z < 0.0f)
            {
                float temp_x = x;
                x            = (1.0f - std::abs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
                y            = (1.0f - std::abs(temp_x)) * (y >= 0.0f ? 1.0f : -1.0f);
            }

            Vector3 direction(x, y, z);
            direction.Normalize();

            return direction;
        }

        // convert float to IEEE 754 half-precision
        uint16_t float_to_half(float value)
        {
            union { float f; uint32_t i; } u = { value };
            uint32_t sign     = (u.i >> 16) & 0x8000;
            int32_t exponent  = ((u.i >> 23) & 0xFF) - 127;
            uint32_t mantissa = u.i & 0x7FFFFF;

            if (exponent <= -15)
            {
                return sign;
            }

            if (exponent > 15)
            {
                return sign | 0x7C00;
            }

            if (exponent <= -14)
            {
                mantissa |= 0x800000;
                mantissa >>= -14 - exponent;
                return sign | mantissa;
            }

            exponent += 15;
            mantissa >>= 13;

            return sign | (exponent << 10) | mantissa;
        }

        // convert IEEE 754 half-precision to float
        float half_to_float(uint16_t value)
        {
            uint32_t sign     = (value & 0x8000) << 16;
            uint32_t exponent = (value >> 10) & 0x1F;
            uint32_t mantissa = value & 0x3FF;

            // inf and nan read as zero
            if (exponent == 0x1F)
            {
                return 0.0f;
            }

            if (exponent == 0 && mantissa == 0)
            {
                return std::bit_cast<float>(sign);
            }

            // denormalized, normalize the mantissa
            if (exponent == 0)
            {
                int shifts = std::countl_zero(mantissa) - 21; // 32 - 11 effective bits
                mantissa <<= shifts;
                exponent = 1 - shifts;
            }

            // half bias 15 to float bias 127
            exponent += 112;

            return std::bit_cast<float>(sign | (exponent << 23) | (mantissa << 13));
        }

        // the rotation that takes up to the given normal
        Quaternion align_up_to(const Vector3& normal)
        {
            const Vector3 up          = Vector3::Up;
            const float up_dot_normal = up.Dot(normal);

            if (std::abs(up_dot_normal) >= 0.999999f)
            {
                return up_dot_normal > 0.0f ? Quaternion::Identity : Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
            }

            const float s          = std::sqrt(2.0f + 2.0f * up_dot_normal);
            const Vector3 cross    = up.Cross(normal) / s;

            return Quaternion(cross.x, cross.y, cross.z, s * 0.5f);
        }

        // scale is stored as a byte on a log curve between these two bounds
        constexpr float scale_minimum = 0.01f;
        constexpr float scale_maximum = 100.0f;
    }

    Matrix Instance::GetMatrix() const
    {
        const Vector3 position(half_to_float(position_x), half_to_float(position_y), half_to_float(position_z));

        const Quaternion align = align_up_to(decode_octahedral(normal_oct));
        const float yaw        = (static_cast<float>(yaw_packed) / 255.0f) * math::pi_2;
        const Quaternion spin(0.0f, std::sin(-yaw * 0.5f), 0.0f, std::cos(-yaw * 0.5f));

        const float t     = static_cast<float>(scale_packed) / 255.0f;
        const float scale = std::exp(std::lerp(std::log(scale_minimum), std::log(scale_maximum), t));

        return Matrix::CreateScale(scale)          *
               Matrix::CreateRotation(align * spin) *
               Matrix::CreateTranslation(position);
    }

    void Instance::SetMatrix(const Matrix& matrix)
    {
        padding = 0;

        const Vector3 position = matrix.GetTranslation();
        position_x             = float_to_half(position.x);
        position_y             = float_to_half(position.y);
        position_z             = float_to_half(position.z);

        const Quaternion rotation = matrix.GetRotation();
        const Vector3 normal      = rotation * Vector3::Up;
        normal_oct                = encode_octahedral(normal);

        // whatever rotation is left once the up alignment is removed is the yaw
        const Quaternion spin = align_up_to(normal).Conjugate() * rotation;
        float yaw             = std::atan2(-spin.y, spin.w) * 2.0f;
        if (yaw < 0.0f)
        {
            yaw += math::pi_2;
        }
        yaw_packed = static_cast<uint8_t>((yaw / math::pi_2) * 255.0f);

        const Vector3 scale = matrix.GetScale();
        float scale_average = (scale.x + scale.y + scale.z) / 3.0f;
        scale_average       = std::clamp(scale_average, scale_minimum, scale_maximum);
        const float t       = (std::log(scale_average) - std::log(scale_minimum)) / (std::log(scale_maximum) - std::log(scale_minimum));
        scale_packed        = static_cast<uint8_t>(t * 255.0f);
    }

    Instance Instance::GetIdentity()
    {
        Instance instance;
        instance.position_x   = 0;
        instance.position_y   = 0;
        instance.position_z   = 0;
        instance.normal_oct   = 0;
        instance.yaw_packed   = 0;
        instance.scale_packed = 0;
        instance.padding      = 0;

        return instance;
    }
}
