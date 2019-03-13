/*
Copyright(c) 2016-2019 Panos Karabelas

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

namespace Directus
{
	struct RHI_Vertex_PosUvNorTan
	{
		RHI_Vertex_PosUvNorTan(){}
		RHI_Vertex_PosUvNorTan(
			const Math::Vector3& position,
			const Math::Vector2& uv,
			const Math::Vector3& normal,
			const Math::Vector3& tangent)
		{
			this->pos[0]	= position.x;
			this->pos[1]	= position.y;
			this->pos[2]	= position.z;

			this->uv[0]		= uv.x;
			this->uv[1]		= uv.y;

			this->normal[0]	= normal.x;
			this->normal[1]	= normal.y;
			this->normal[2]	= normal.z;

			this->tangent[0]	= tangent.x;
			this->tangent[1]	= tangent.y;
			this->tangent[2]	= tangent.z;
		}

		float pos[3]		= { 0 };
		float uv[2]			= { 0 };
		float normal[3]		= { 0 };
		float tangent[3]	= { 0 };
	};

	struct RHI_Vertex_PosUVNor
	{
		RHI_Vertex_PosUVNor(){}
		RHI_Vertex_PosUVNor(const Math::Vector3& position, const Math::Vector2& uv, const Math::Vector3& normal)
		{
			this->pos[0] = position.x;
			this->pos[1] = position.y;
			this->pos[2] = position.z;

			this->uv[0] = uv.x;
			this->uv[1] = uv.y;

			this->normal[0] = normal.x;
			this->normal[1] = normal.y;
			this->normal[2] = normal.z;
		}

		float pos[3]	= { 0 };
		float uv[2]		= { 0 };
		float normal[3] = { 0 };
	};

	struct RHI_Vertex_PosUV
	{
		RHI_Vertex_PosUV(){}

		RHI_Vertex_PosUV(float posX, float posY, float posZ, float uvX, float uvY)
		{
			pos[0] = posX;
			pos[1] = posY;
			pos[2] = posZ;

			uv[0] = uvX;
			uv[1] = uvY;
		}

		RHI_Vertex_PosUV(const Math::Vector3& position, const Math::Vector2& uv)
		{
			this->pos[0]	= position.x;
			this->pos[1]	= position.y;
			this->pos[2]	= position.z;

			this->uv[0]		= uv.x;
			this->uv[1]		= uv.y;
		}

		float pos[3]		= { 0 };
		float uv[2]			= { 0 };
	};

	struct RHI_Vertex_PosCol
	{
		RHI_Vertex_PosCol(){}
		RHI_Vertex_PosCol(const Math::Vector3& position, const Math::Vector4& color)
		{
			this->pos[0]	= position.x;
			this->pos[1]	= position.y;
			this->pos[2]	= position.z;

			this->color[0]	= color.x;
			this->color[1]	= color.y;
			this->color[2]	= color.z;
			this->color[3]	= color.w;
		}

		float pos[3]	= {0};
		float color[4]	= {0};
	};

	static_assert(std::is_trivially_copyable<RHI_Vertex_PosUvNorTan>::value,	"RHI_Vertex_PosUVTBN is not trivially copyable");
	static_assert(std::is_trivially_copyable<RHI_Vertex_PosUVNor>::value,		"RHI_Vertex_PosUVNor is not trivially copyable");
	static_assert(std::is_trivially_copyable<RHI_Vertex_PosUV>::value,			"RHI_Vertex_PosUV is not trivially copyable");
	static_assert(std::is_trivially_copyable<RHI_Vertex_PosCol>::value,			"RHI_Vertex_PosCol is not trivially copyable");
}