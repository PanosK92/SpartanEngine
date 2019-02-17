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

//= INCLUDES =====================
#include "../Core/BackendConfig.h"
//================================

// RHI (Rendering Hardware Interface) definitions
namespace Directus
{
	class RHI_Device;
	class RHI_SwapChain;
	class RHI_VertexBuffer;
	class RHI_IndexBuffer;
	class RHI_ConstantBuffer;
	class RHI_Sampler;
	class RHI_Pipeline;
	class RHI_Viewport;
	class RHI_RenderTexture;
	class RHI_Texture;
	class RHI_Shader;
	class RHI_RasterizerState;
	class RHI_BlendState;
	class RHI_DepthStencilState;
	class RHI_InputLayout;
	struct RHI_Vertex_PosUvNorTan;
	struct RHI_Vertex_PosUVNor;
	struct RHI_Vertex_PosUV;
	struct RHI_Vertex_PosCol;

	enum RHI_Present_Mode
	{
		Present_Off,
		Present_VerticalBlank,
		Present_SecondVerticalBlank
	};
	enum RHI_Swap_Effect
	{
		Swap_Discard,
		Swap_Sequential,
		Swap_Flip_Sequential,
		Swap_Flip_Discard
	};

	enum RHI_SwapChain_Flag
	{
		SwapChain_Allow_Mode_Switch,
		SwapChain_Allow_Tearing
	};

	enum RHI_Query_Type
	{
		Query_Timestamp,
		Query_Timestamp_Disjoint
	};

	enum RHI_Clear_Buffer
	{
		Clear_Depth		= 1 << 0,
		Clear_Stencil	= 1 << 1
	};

	enum RHI_Buffer_Scope
	{
		Buffer_VertexShader,
		Buffer_PixelShader,
		Buffer_Global
	};

	enum RHI_PrimitiveTopology_Mode
	{
		PrimitiveTopology_TriangleList,
		PrimitiveTopology_LineList,
		PrimitiveTopology_NotAssigned
	};

	enum RHI_Input_Element : unsigned long
	{
		Input_Position2D	= 1UL << 0,
		Input_Position3D	= 1UL << 1,
		Input_Color8		= 1UL << 2,
		Input_Color32		= 1UL << 3,
		Input_Texture		= 1UL << 4,
		Input_NormalTangent = 1UL << 5,
		Input_ImGui			= 1UL << 6
	};
	#define Input_PositionColor					0 | Input_Position3D	| Input_Color32
	#define Input_PositionTexture				0 | Input_Position3D	| Input_Texture
	#define Input_PositionTextureNormalTangent	0 | Input_Position3D	| Input_Texture | Input_NormalTangent
	#define Input_Position2DTextureColor8		0 | Input_Position2D	| Input_Texture | Input_Color8

	enum RHI_Cull_Mode
	{
		Cull_None,
		Cull_Front,
		Cull_Back
	};

	enum RHI_Fill_Mode
	{
		Fill_Solid,
		Fill_Wireframe
	};

	enum RHI_Texture_Filter
	{
		Texture_Filter_Comparison_Point,
		Texture_Filter_Comparison_Bilinear,
		Texture_Filter_Comparison_Trilinear,
		Texture_Filter_Point,
		Texture_Filter_Bilinear,
		Texture_Filter_Trilinear,
		Texture_Filter_Anisotropic
	};

	enum RHI_Texture_Address_Mode
	{
		Texture_Address_Wrap,
		Texture_Address_Mirror,
		Texture_Address_Clamp,
		Texture_Address_Border,
		Texture_Address_MirrorOnce,
	};

	enum RHI_Comparison_Function
	{
		Comparison_Never,
		Comparison_Less,
		Comparison_Equal,
		Comparison_LessEqual,
		Comparison_Greater,
		Comparison_NotEqual,
		Comparison_GreaterEqual,
		Comparison_Always
	};

	enum RHI_Format
	{
		// R
		Format_R8_UNORM,
		Format_R16_UINT,
		Format_R16_FLOAT,
		Format_R32_UINT,
		Format_R32_FLOAT,
		Format_D32_FLOAT,
		// RG
		Format_R8G8_UNORM,
		Format_R16G16_FLOAT,
		Format_R32G32_FLOAT,	
		// RGB
		Format_R32G32B32_FLOAT,
		// RGBA
		Format_R8G8B8A8_UNORM,
		Format_R16G16B16A16_FLOAT,
		Format_R32G32B32A32_FLOAT
	};

	enum RHI_Blend
	{
		Blend_Zero,
		Blend_One,
		Blend_Src_Color,
		Blend_Inv_Src_Color,
		Blend_Src_Alpha,
		Blend_Inv_Src_Alpha
	};

	enum RHI_Blend_Operation
	{
		Blend_Operation_Add,
		Blend_Operation_Subtract,
		Blend_Operation_Rev_Subtract,
		Blend_Operation_Min,
		Blend_Operation_Max
	};
}