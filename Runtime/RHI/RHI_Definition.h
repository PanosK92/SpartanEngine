/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES =====================
#include "../Core/BackendConfig.h"
//================================

// RHI (Rendering Hardware Interface) definitions
namespace Directus
{
	class RHI_Device;
	class RHI_VertexBuffer;
	class RHI_IndexBuffer;
	class RHI_ConstantBuffer;
	class RHI_Sampler;
	class RHI_Pipeline;
	class RHI_Viewport;
	class RHI_RenderTexture;
	class RHI_Texture;
	class RHI_Shader;
	class RHI_InputLayout;
	struct RHI_Vertex_PosUVTBN;
	struct RHI_Vertex_PosUVNor;
	struct RHI_Vertex_PosUV;
	struct RHI_Vertex_PosCol;

	enum Query_Type
	{
		Query_Timestamp,
		Query_Timestamp_Disjoint
	};

	enum Clear_Buffer
	{
		Clear_Depth		= 1 << 0,
		Clear_Stencil	= 1 << 1
	};

	enum Buffer_Scope
	{
		Buffer_VertexShader,
		Buffer_PixelShader,
		Buffer_Global
	};

	enum PrimitiveTopology_Mode
	{
		PrimitiveTopology_TriangleList,
		PrimitiveTopology_LineList,
		PrimitiveTopology_NotAssigned
	};

	enum Input_Layout
	{
		Input_Position,
		Input_PositionColor,
		Input_PositionTexture,
		Input_PositionTextureTBN,
		Input_NotAssigned
	};

	enum Cull_Mode
	{
		Cull_None,
		Cull_Front,
		Cull_Back,
		Cull_NotAssigned
	};

	enum Fill_Mode
	{
		Fill_Solid,
		Fill_Wireframe,
		Fill_NotAssigned
	};

	enum Texture_Sampler_Filter
	{
		Texture_Sampler_Comparison_Point,
		Texture_Sampler_Comparison_Bilinear,
		Texture_Sampler_Comparison_Trilinear,
		Texture_Sampler_Point,
		Texture_Sampler_Bilinear,
		Texture_Sampler_Trilinear,
		Texture_Sampler_Anisotropic
	};

	enum Texture_Address_Mode
	{
		Texture_Address_Wrap,
		Texture_Address_Mirror,
		Texture_Address_Clamp,
		Texture_Address_Border,
		Texture_Address_MirrorOnce,
	};

	enum Texture_Comparison_Function
	{
		Texture_Comparison_Never,
		Texture_Comparison_Less,
		Texture_Comparison_Equal,
		Texture_Comparison_LessEqual,
		Texture_Comparison_Greater,
		Texture_Comparison_NotEqual,
		Texture_Comparison_GreaterEqual,
		Texture_Comparison_Always
	};

	enum Texture_Format
	{
		Texture_Format_R8_UNORM,
		Texture_Format_R8G8B8A8_UNORM,
		Texture_Format_R16_FLOAT,
		Texture_Format_R32_FLOAT,
		Texture_Format_R16G16_FLOAT,
		Texture_Format_R32G32_FLOAT,
		Texture_Format_R32G32B32_FLOAT,
		Texture_Format_R16G16B16A16_FLOAT,
		Texture_Format_R32G32B32A32_FLOAT,
		Texture_Format_D32_FLOAT
	};
}