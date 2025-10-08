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

//= includes ==============
#include "../Math/Matrix.h"
#include <bit>
//=========================

namespace spartan
{
    #pragma pack(push, 1)
    struct Instance
    {
        uint16_t position_x;  // 2 bytes
        uint16_t position_y;  // 2 bytes
        uint16_t position_z;  // 2 bytes
        uint16_t normal_oct;  // 2 bytes
        uint8_t yaw_packed;   // 1 byte
        uint8_t scale_packed; // 1 byte
                              // total: 10 bytes

        math::Matrix GetMatrix() const
        {
            // compose position
            math::Vector3 position(half_to_float(position_x), half_to_float(position_y), half_to_float(position_z));

            // compose rotation
            math::Vector3 normal = decode_octahedral(normal_oct);
            math::Vector3 up     = math::Vector3::Up;
            float up_dot_normal  = up.Dot(normal);
            math::Quaternion quat_align;
            if (std::abs(up_dot_normal) >= 0.999999f)
            {
                quat_align = up_dot_normal > 0.0f ? math::Quaternion::Identity : math::Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
            }
            else
            {
                float s                  = std::sqrt(2.0f + 2.0f * up_dot_normal);
                math::Vector3 cross_prod = up.Cross(normal) / s;
                quat_align               = math::Quaternion(cross_prod.x, cross_prod.y, cross_prod.z, s * 0.5f);
            }
            float yaw = (static_cast<float>(yaw_packed) * 0.003906250f) * math::pi_2;
            math::Quaternion quat_yaw(0.0f, std::sin(-yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f));
            math::Quaternion quat = quat_align * quat_yaw;

            // compose scale
            float t = static_cast<float>(scale_packed) * 0.003906250f;
            float scale_float = std::exp(std::lerp(std::log(0.01f), std::log(100.0f), t));

            // compose matrix
            return math::Matrix::CreateScale(scale_float) *
                   math::Matrix::CreateRotation(quat)     *
                   math::Matrix::CreateTranslation(position);
        }

        void SetMatrix(const math::Matrix& matrix)
        {
            // pack position
            math::Vector3 position = matrix.GetTranslation();
            position_x             = float_to_half(position.x);
            position_y             = float_to_half(position.y);
            position_z             = float_to_half(position.z);

            // pack normal
            math::Quaternion quat = matrix.GetRotation();
            math::Vector3 normal  = quat * math::Vector3::Up;
            normal_oct            = encode_octahedral(normal);

            // pack yaw
            math::Vector3 up    = math::Vector3::Up;
            float up_dot_normal = up.Dot(normal);
            math::Quaternion quat_align;
            if (std::abs(up_dot_normal) >= 0.999999f)
            {
                quat_align = up_dot_normal > 0.0f ? math::Quaternion::Identity : math::Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
            }
            else
            {
                float s                  = std::sqrt(2.0f + 2.0f * up_dot_normal);
                math::Vector3 cross_prod = up.Cross(normal) / s;
                quat_align               = math::Quaternion(cross_prod.x, cross_prod.y, cross_prod.z, s * 0.5f);
            }
            math::Quaternion quat_yaw  = quat_align.Conjugate() * quat;
            float half_angle           = std::atan2(-quat_yaw.y, quat_yaw.w);
            float yaw                  = half_angle * 2.0f;
            if (yaw < 0.0f) yaw       += math::pi_2;
            yaw_packed                 = static_cast<uint8_t>((yaw / math::pi_2) * 255.0f);

            // pack scale
            float scale_avg = (matrix.GetScale().x + matrix.GetScale().y + matrix.GetScale().z) / 3.0f;
            scale_avg       = std::max(0.01f, std::min(100.0f, scale_avg));
            float t         = (std::log(scale_avg) - std::log(0.01f)) / (std::log(100.0f) - std::log(0.01f));
            scale_packed    = static_cast<uint8_t>(t * 255.0f);
        }

        static Instance GetIdentity()
        {
            Instance instance;
            instance.position_x   = 0;
            instance.position_y   = 0;
            instance.position_z   = 0;
            instance.normal_oct   = 0;
            instance.yaw_packed   = 0;
            instance.scale_packed = 0;

            return instance;
        }

        static uint16_t encode_octahedral(const math::Vector3& dir)
        {
            math::Vector3 oct = dir / (std::abs(dir.x) + std::abs(dir.y) + std::abs(dir.z));
            if (oct.z < 0.0f)
            {
                float temp_x = oct.x;
                oct.x = (1.0f - std::abs(oct.y)) * (temp_x >= 0.0f ? 1.0f : -1.0f);
                oct.y = (1.0f - std::abs(temp_x)) * (oct.y >= 0.0f ? 1.0f : -1.0f);
            }
            uint8_t ox = static_cast<uint8_t>(std::round((oct.x * 0.5f + 0.5f) * 255.0f));
            uint8_t oy = static_cast<uint8_t>(std::round((oct.y * 0.5f + 0.5f) * 255.0f));
            return (static_cast<uint16_t>(ox) << 8) | oy;
        }

        static math::Vector3 decode_octahedral(uint16_t packed)
        {
            float x = (static_cast<float>(packed >> 8) * 0.003906250f) * 2.0f - 1.0f;
            float y = (static_cast<float>(packed & 0xFF) * 0.003906250f) * 2.0f - 1.0f;
            float z = 1.0f - std::abs(x) - std::abs(y);
            if (z < 0.0f)
            {
                float temp_x = x;
                x = (1.0f - std::abs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
                y = (1.0f - std::abs(temp_x)) * (y >= 0.0f ? 1.0f : -1.0f);
            }
            math::Vector3 dir(x, y, z);
            dir.Normalize();
            return dir;
        }

        // convert float to IEEE 754 half-precision
        static uint16_t float_to_half(float value)
        {
            union { float f; uint32_t i; } u = { value };
            uint32_t sign = (u.i >> 16) & 0x8000;
            int32_t exponent = ((u.i >> 23) & 0xFF) - 127;
            uint32_t mantissa = u.i & 0x7FFFFF;
            if (exponent <= -15) return sign;
            if (exponent > 15) return sign | 0x7C00;
            if (exponent <= -14)
            {
                mantissa |= 0x800000;
                int shift = -14 - exponent;
                mantissa >>= shift;
                return sign | mantissa;
            }
            exponent += 15;
            mantissa >>= 13;
            return sign | (exponent << 10) | mantissa;
        }

        // convert IEEE 754 half-precision to float
        static float half_to_float(uint16_t value)
        {
            // extract components
            uint32_t sign = (value & 0x8000) << 16;
            uint32_t exp  = (value >> 10) & 0x1F;
            uint32_t mant = value & 0x3FF;
        
            // handle inf/nan as 0
            if (exp == 0x1F)
            {
                return 0.0f;
            }
        
            // zero
            if (exp == 0 && mant == 0)
            {
                return std::bit_cast<float>(sign);
            }
        
            // denormalized
            if (exp == 0)
            {
                // normalize mantissa
                int shifts = std::countl_zero(mant) - 21; // 32 - 11 effective bits
                mant <<= shifts;
                exp = 1 - shifts;
            }
        
            // half 15 to float 127
            exp += 112; // 127 - 15 = 112
        
            // assemble float bits
            return std::bit_cast<float>(sign | (exp << 23) | (mant << 13));
        }
    };
    #pragma pack(pop)
}
