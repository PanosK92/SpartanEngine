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

//= INCLUDES ===============
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
//==========================

namespace Spartan
{
    struct RHI_Vertex_Undefined{};

    struct RHI_Vertex_Pos
    {
        RHI_Vertex_Pos(const Math::Vector3& position)
        {
            this->pos[0] = position.x;
            this->pos[1] = position.y;
            this->pos[2] = position.z;
        }

        float pos[3] = { 0 };
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

        RHI_Vertex_PosTex(const Math::Vector3& pos, const Math::Vector2& tex)
        {
            this->pos[0] = pos.x;
            this->pos[1] = pos.y;
            this->pos[2] = pos.z;

            this->tex[0] = tex.x;
            this->tex[1] = tex.y;
        }

        float pos[3] = { 0 };
        float tex[2] = { 0 };
    };

    struct RHI_Vertex_PosCol
    {
        RHI_Vertex_PosCol() = default;

        RHI_Vertex_PosCol(const Math::Vector3& pos, const Math::Vector4& col)
        {
            this->pos[0] = pos.x;
            this->pos[1] = pos.y;
            this->pos[2] = pos.z;

            this->col[0] = col.x;
            this->col[1] = col.y;
            this->col[2] = col.z;
            this->col[3] = col.w;
        }

        float pos[3]    = {0};
        float col[4]    = {0};
    };

    struct RHI_Vertex_Pos2dTexCol8
    {
        RHI_Vertex_Pos2dTexCol8() = default;

        float pos[2] = { 0 };
        float tex[2] = { 0 };
        uint32_t col = 0;
    };

    struct RHI_Vertex_PosTexNorTan
    {
        RHI_Vertex_PosTexNorTan() = default;
        RHI_Vertex_PosTexNorTan(
            const Math::Vector3& pos,
            const Math::Vector2& tex,
            const Math::Vector3& nor = Math::Vector3::Zero,
            const Math::Vector3& tan = Math::Vector3::Zero)
        {
            this->pos[0] = pos.x;
            this->pos[1] = pos.y;
            this->pos[2] = pos.z;

            this->tex[0] = tex.x;
            this->tex[1] = tex.y;

            this->nor[0] = nor.x;
            this->nor[1] = nor.y;
            this->nor[2] = nor.z;

            this->tan[0] = tan.x;
            this->tan[1] = tan.y;
            this->tan[2] = tan.z;
        }

        float pos[3] = { 0 };
        float tex[2] = { 0 };
        float nor[3] = { 0 };
        float tan[3] = { 0 };
    };

    static_assert(std::is_trivially_copyable<RHI_Vertex_Pos>::value,            "RHI_Vertex_Pos is not trivially copyable");
    static_assert(std::is_trivially_copyable<RHI_Vertex_PosTex>::value,            "RHI_Vertex_PosTex is not trivially copyable");
    static_assert(std::is_trivially_copyable<RHI_Vertex_PosCol>::value,            "RHI_Vertex_PosCol is not trivially copyable");
    static_assert(std::is_trivially_copyable<RHI_Vertex_Pos2dTexCol8>::value,    "RHI_Vertex_Pos2dTexCol8 is not trivially copyable");
    static_assert(std::is_trivially_copyable<RHI_Vertex_PosTexNorTan>::value,    "RHI_Vertex_PosTexNorTan is not trivially copyable");

    enum RHI_Vertex_Type
    {
        RHI_Vertex_Type_Unknown,
        RHI_Vertex_Type_Position,
        RHI_Vertex_Type_PositionColor,
        RHI_Vertex_Type_PositionTexture,
        RHI_Vertex_Type_PositionTextureNormalTangent,
        RHI_Vertex_Type_Position2dTextureColor8
    };

    template <typename T>
    constexpr RHI_Vertex_Type RHI_Vertex_Type_To_Enum() { return RHI_Vertex_Type_Unknown; }
    // Explicit template instantiation
    template<> inline RHI_Vertex_Type RHI_Vertex_Type_To_Enum<RHI_Vertex_Pos>()                { return RHI_Vertex_Type_Position; }
    template<> inline RHI_Vertex_Type RHI_Vertex_Type_To_Enum<RHI_Vertex_PosTex>()            { return RHI_Vertex_Type_PositionTexture; }
    template<> inline RHI_Vertex_Type RHI_Vertex_Type_To_Enum<RHI_Vertex_PosCol>()            { return RHI_Vertex_Type_PositionColor; }
    template<> inline RHI_Vertex_Type RHI_Vertex_Type_To_Enum<RHI_Vertex_Pos2dTexCol8>()    { return RHI_Vertex_Type_Position2dTextureColor8; }
    template<> inline RHI_Vertex_Type RHI_Vertex_Type_To_Enum<RHI_Vertex_PosTexNorTan>()    { return RHI_Vertex_Type_PositionTextureNormalTangent; }
}
