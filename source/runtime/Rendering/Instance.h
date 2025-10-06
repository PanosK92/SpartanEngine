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

#pragma once

//= INCLUDES ==============
#include "../Math/Matrix.h"
#include <bit>
//=========================

namespace spartan
{
    struct Instance
    {
        math::Vector3 position; // 12 bytes
        math::Vector3 rotation; // 12 bytes
        uint16_t scale;         // 2 bytes

        math::Matrix GetMatrix() const
        {
            float scale_float = half_to_float(scale);
            float w           = sqrtf(std::max(0.0f, 1.0f - rotation.LengthSquared()));
            math::Quaternion quat(rotation.x, rotation.y, rotation.z, w);

            return math::Matrix::CreateScale(scale_float) *
                   math::Matrix::CreateRotation(quat) *
                   math::Matrix::CreateTranslation(position);
        }

        void SetMatrix(const math::Matrix& matrix)
        {
            position              = matrix.GetTranslation();
            math::Quaternion quat = matrix.GetRotation();
            rotation              = math::Vector3(quat.x, quat.y, quat.z);
            float scale_avg       = (matrix.GetScale().x + matrix.GetScale().y + matrix.GetScale().z) / 3.0f;
            scale                 = float_to_half(scale_avg);
        }

        // convert float to IEEE 754 half-precision
        static uint16_t float_to_half(float value)
        {
            union { float f; uint32_t i; } u = { value };
            uint32_t sign     = (u.i >> 16) & 0x8000;
            int32_t exponent  = ((u.i >> 23) & 0xFF) - 127;
            uint32_t mantissa = u.i & 0x7FFFFF;

            if (exponent <= -15) // underflow to zero or subnormal
                return sign;

            if (exponent > 15) // overflow to infinity
                return sign | 0x7C00;

            if (exponent <= -14) // subnormal
            {
                mantissa |= 0x800000;
                int shift = -14 - exponent;
                mantissa >>= shift;
                return sign | mantissa;
            }

            // normal
            exponent += 15;
            mantissa >>= 13; // 10-bit mantissa
            return sign | (exponent << 10) | mantissa;
        }

        // convert IEEE 754 half-precision to float
        static float half_to_float(uint16_t value)
        {
            uint32_t sign     = (value & 0x8000) << 16;
            int32_t exponent  = ((value >> 10) & 0x1F) - 15;
            uint32_t mantissa = value & 0x3FF;

            if (exponent == -15) // subnormal or zero
            {
                if (mantissa == 0)
                    return std::bit_cast<float>(sign);

                float m = mantissa / 1024.0f;
                return std::bit_cast<float>(sign | ((exponent + 127) << 23) | (mantissa << 13)) * 0.00006103515625f; // 2^-14
            }

            if (exponent == 16) // infinity or NaN
                return std::bit_cast<float>(sign | 0x7F800000);

            return std::bit_cast<float>(sign | ((exponent + 127) << 23) | (mantissa << 13));
        }
    };
}
