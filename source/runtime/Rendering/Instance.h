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
    #pragma pack(push, 1)
    struct Instance
    {
        math::Vector3 position; // 12 bytes
        uint32_t rotation;      // 4 bytes
        uint16_t scale;         // 2 bytes

        math::Matrix GetMatrix() const
        {
            float scale_float     = half_to_float(scale);
            math::Quaternion quat = decode_quaternion(rotation);
            return math::Matrix::CreateScale(scale_float) *
                   math::Matrix::CreateRotation(quat) *
                   math::Matrix::CreateTranslation(position);
        }

        void SetMatrix(const math::Matrix& matrix)
        {
            position              = matrix.GetTranslation();
            math::Quaternion quat = matrix.GetRotation();
            rotation              = encode_quaternion(quat);
            float scale_avg       = (matrix.GetScale().x + matrix.GetScale().y + matrix.GetScale().z) / 3.0f;
            scale                 = float_to_half(scale_avg);
        }

        static Instance GetIdentity()
        {
            Instance instance;
            instance.position = math::Vector3::Zero;
            instance.rotation = 0;
            instance.scale    = 0;

            return instance;
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

        // encode quaternion to 32 bits (smallest-three)
        static uint32_t encode_quaternion(const math::Quaternion& quat)
        {
            // find largest component
            float abs_x = std::abs(quat.x);
            float abs_y = std::abs(quat.y);
            float abs_z = std::abs(quat.z);
            float abs_w = std::abs(quat.w);
            float max_value = std::max(std::max(std::max(abs_x, abs_y), abs_z), abs_w);
            uint32_t largest_index = 0;
            float largest_value = abs_w;
            float a, b, c; // the three smallest components
            if (abs_x > largest_value) { largest_index = 1; largest_value = abs_x; }
            if (abs_y > largest_value) { largest_index = 2; largest_value = abs_y; }
            if (abs_z > largest_value) { largest_index = 3; largest_value = abs_z; }

            // assign smallest components
            if (largest_index == 0) { a = quat.x; b = quat.y; c = quat.z; }
            else if (largest_index == 1) { a = quat.y; b = quat.z; c = quat.w; }
            else if (largest_index == 2) { a = quat.x; b = quat.z; c = quat.w; }
            else { a = quat.x; b = quat.y; c = quat.w; }

            // normalize to [-0.707, 0.707] and scale to 10-bit
            const float scale = 1.41421356237f; // sqrt(2)
            a = (a / scale + 1.0f) * 0.5f * 1023.0f;
            b = (b / scale + 1.0f) * 0.5f * 1023.0f;
            c = (c / scale + 1.0f) * 0.5f * 1023.0f;

            // pack into 32 bits: 2 bits for index, 10 bits per component
            uint32_t result = (largest_index << 30);
            result |= (static_cast<uint32_t>(a) & 0x3FF) << 20;
            result |= (static_cast<uint32_t>(b) & 0x3FF) << 10;
            result |= (static_cast<uint32_t>(c) & 0x3FF);

            return result;
        }

        // decode quaternion from 32 bits
        static math::Quaternion decode_quaternion(uint32_t packed)
        {
            // extract components
            uint32_t largest_index = (packed >> 30) & 0x3;
            float a = static_cast<float>((packed >> 20) & 0x3FF) / 1023.0f * 2.0f - 1.0f;
            float b = static_cast<float>((packed >> 10) & 0x3FF) / 1023.0f * 2.0f - 1.0f;
            float c = static_cast<float>(packed & 0x3FF) / 1023.0f * 2.0f - 1.0f;
            const float scale = 1.41421356237f; // sqrt(2)
            a *= scale; b *= scale; c *= scale;

            // compute largest component
            float largest = sqrtf(std::max(0.0f, 1.0f - (a * a + b * b + c * c)));

            // reconstruct quaternion
            if (largest_index == 0) return math::Quaternion(a, b, c, largest);
            if (largest_index == 1) return math::Quaternion(largest, a, b, c);
            if (largest_index == 2) return math::Quaternion(a, largest, b, c);

            return math::Quaternion(a, b, largest, c);
        }
    };
    #pragma pack(pop)
}
