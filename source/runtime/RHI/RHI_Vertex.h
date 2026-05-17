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

#pragma once

//= INCLUDES ==================
#include <cmath>
#include <cstdint>
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Rendering/Color.h"
#include <Definitions.h>
//=============================

namespace spartan
{
    // shared packing helpers used by the 24-byte main vertex
    namespace vertex_pack
    {
        // ieee 754 half conversion, matches the helper in instance.h
        inline uint16_t float_to_half(float value)
        {
            union { float f; uint32_t i; } u = { value };
            uint32_t sign     = (u.i >> 16) & 0x8000;
            int32_t  exponent = ((u.i >> 23) & 0xFF) - 127;
            uint32_t mantissa = u.i & 0x7FFFFF;
            if (exponent <= -15)
            {
                return static_cast<uint16_t>(sign);
            }
            if (exponent >   15)
            {
                return static_cast<uint16_t>(sign | 0x7C00);
            }
            if (exponent <= -14)
            {
                mantissa |= 0x800000;
                int shift = -14 - exponent;
                mantissa >>= shift;
                return static_cast<uint16_t>(sign | mantissa);
            }
            exponent += 15;
            mantissa >>= 13;
            return static_cast<uint16_t>(sign | (exponent << 10) | mantissa);
        }

        inline float half_to_float(uint16_t value)
        {
            uint32_t sign = static_cast<uint32_t>(value & 0x8000) << 16;
            uint32_t exp  = (value >> 10) & 0x1F;
            uint32_t mant = value & 0x3FF;
            if (exp == 0x1F)
            {
                return 0.0f;
            }
            if (exp == 0 && mant == 0)
            {
                union { uint32_t i; float f; } u = { sign };
                return u.f;
            }
            if (exp == 0)
            {
                int shifts = 0;
                while ((mant & 0x400) == 0) { mant <<= 1; shifts++; }
                mant &= 0x3FF;
                exp = 1 - shifts;
            }
            exp += 112; // 127 - 15
            union { uint32_t i; float f; } u = { sign | (exp << 23) | (mant << 13) };
            return u.f;
        }

        // pack two floats into a single uint as half2
        inline uint32_t pack_half2(float x, float y)
        {
            return static_cast<uint32_t>(float_to_half(x)) | (static_cast<uint32_t>(float_to_half(y)) << 16);
        }

        inline math::Vector2 unpack_half2(uint32_t packed)
        {
            uint16_t h0 = static_cast<uint16_t>(packed & 0xFFFF);
            uint16_t h1 = static_cast<uint16_t>((packed >> 16) & 0xFFFF);
            return math::Vector2(half_to_float(h0), half_to_float(h1));
        }

        // octahedral encoding into two snorm16 channels packed in a single uint
        // matches the shader-side decode (oct snorm 16:16, x in low 16 bits, y in high 16 bits)
        inline uint32_t pack_oct_snorm16(const math::Vector3& dir)
        {
            float ax  = std::fabs(dir.x);
            float ay  = std::fabs(dir.y);
            float az  = std::fabs(dir.z);
            float inv = 1.0f / std::max(ax + ay + az, 1e-20f);
            float ox  = dir.x * inv;
            float oy  = dir.y * inv;
            if (dir.z < 0.0f)
            {
                float tx = ox;
                ox = (1.0f - std::fabs(oy)) * (tx >= 0.0f ? 1.0f : -1.0f);
                oy = (1.0f - std::fabs(tx)) * (oy >= 0.0f ? 1.0f : -1.0f);
            }
            int32_t qx = static_cast<int32_t>(std::lround(std::clamp(ox, -1.0f, 1.0f) * 32767.0f));
            int32_t qy = static_cast<int32_t>(std::lround(std::clamp(oy, -1.0f, 1.0f) * 32767.0f));
            return (static_cast<uint32_t>(static_cast<uint16_t>(qx)))
                 | (static_cast<uint32_t>(static_cast<uint16_t>(qy)) << 16);
        }

        inline math::Vector3 unpack_oct_snorm16(uint32_t packed)
        {
            int16_t sx = static_cast<int16_t>(packed & 0xFFFF);
            int16_t sy = static_cast<int16_t>((packed >> 16) & 0xFFFF);
            float x    = std::max(-1.0f, static_cast<float>(sx) / 32767.0f);
            float y    = std::max(-1.0f, static_cast<float>(sy) / 32767.0f);
            float z    = 1.0f - std::fabs(x) - std::fabs(y);
            if (z < 0.0f)
            {
                float tx = x;
                x = (1.0f - std::fabs(y))  * (x  >= 0.0f ? 1.0f : -1.0f);
                y = (1.0f - std::fabs(tx)) * (y  >= 0.0f ? 1.0f : -1.0f);
            }
            math::Vector3 r(x, y, z);
            r.Normalize();
            return r;
        }
    }

    struct RHI_Vertex_Pos
    {
        RHI_Vertex_Pos(const math::Vector3& position)
        {
            this->pos[0] = position.x;
            this->pos[1] = position.y;
            this->pos[2] = position.z;
        }

        float pos[3] = { 0, 0, 0 };
    };

    struct RHI_Vertex_PosTex
    {
        RHI_Vertex_PosTex(const float pos_x, const float pos_y, const float pos_z, const float tex_x, const float tex_y)
        {
            pos[0] = pos_x;
            pos[1] = pos_y;
            pos[2] = pos_z;

            tex[0] = tex_x;
            tex[1] = tex_y;
        }

        RHI_Vertex_PosTex(const math::Vector3& pos, const math::Vector2& tex)
        {
            this->pos[0] = pos.x;
            this->pos[1] = pos.y;
            this->pos[2] = pos.z;

            this->tex[0] = tex.x;
            this->tex[1] = tex.y;
        }

        float pos[3] = { 0, 0, 0 };
        float tex[2] = { 0, 0 };
    };

    struct RHI_Vertex_PosCol
    {
        RHI_Vertex_PosCol() = default;

        RHI_Vertex_PosCol(const math::Vector3& pos, const Color& col)
        {
            this->pos[0] = pos.x;
            this->pos[1] = pos.y;
            this->pos[2] = pos.z;

            this->col[0] = col.r;
            this->col[1] = col.g;
            this->col[2] = col.b;
            this->col[3] = col.a;
        }

        float pos[3] = { 0, 0, 0 };
        float col[4] = { 0, 0, 0, 0};
    };

    struct RHI_Vertex_Pos2dTexCol8
    {
        RHI_Vertex_Pos2dTexCol8() = default;

        float pos[2] = { 0, 0 };
        float tex[2] = { 0, 0 };
        uint32_t col = 0;
    };

    // main mesh vertex, 24 bytes
    // pos kept as float3 for meshopt and bbox/picking precision
    // uv as half2, normal/tangent as octahedral snorm 16:16
    struct RHI_Vertex_PosTexNorTan
    {
        RHI_Vertex_PosTexNorTan() = default;
        RHI_Vertex_PosTexNorTan(
            const math::Vector3& position,
            const math::Vector2& texcoord,
            const math::Vector3& normal  = math::Vector3::Zero,
            const math::Vector3& tangent = math::Vector3::Zero)
        {
            set_position(position);
            set_uv(texcoord);
            set_normal(normal);
            set_tangent(tangent);
        }

        // raw position storage, exposed for meshopt and bbox math that read pos[0..2] directly
        float    pos[3] = { 0.0f, 0.0f, 0.0f }; // 12
        uint32_t uv     = 0;                    // 4, half2
        uint32_t nor    = 0;                    // 4, oct snorm 16:16
        uint32_t tan    = 0;                    // 4, oct snorm 16:16

        void set_position(const math::Vector3& v) { pos[0] = v.x; pos[1] = v.y; pos[2] = v.z; }
        math::Vector3 get_position() const        { return math::Vector3(pos[0], pos[1], pos[2]); }

        void set_uv(const math::Vector2& v) { uv = vertex_pack::pack_half2(v.x, v.y); }
        void set_uv(float x, float y)       { uv = vertex_pack::pack_half2(x, y); }
        math::Vector2 get_uv() const        { return vertex_pack::unpack_half2(uv); }

        void set_normal(const math::Vector3& v) { nor = vertex_pack::pack_oct_snorm16(v); }
        math::Vector3 get_normal() const        { return vertex_pack::unpack_oct_snorm16(nor); }

        void set_tangent(const math::Vector3& v) { tan = vertex_pack::pack_oct_snorm16(v); }
        math::Vector3 get_tangent() const        { return vertex_pack::unpack_oct_snorm16(tan); }
    };

    SP_ASSERT_STATIC_IS_TRIVIALLY_COPYABLE(RHI_Vertex_Pos);
    SP_ASSERT_STATIC_IS_TRIVIALLY_COPYABLE(RHI_Vertex_PosTex);
    SP_ASSERT_STATIC_IS_TRIVIALLY_COPYABLE(RHI_Vertex_PosCol);
    SP_ASSERT_STATIC_IS_TRIVIALLY_COPYABLE(RHI_Vertex_Pos2dTexCol8);
    SP_ASSERT_STATIC_IS_TRIVIALLY_COPYABLE(RHI_Vertex_PosTexNorTan);

    static_assert(sizeof(RHI_Vertex_PosTexNorTan) == 24, "main vertex must be 24 bytes");
}
