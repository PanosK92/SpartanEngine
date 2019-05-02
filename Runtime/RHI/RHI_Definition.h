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

// RHI (Rendering Hardware Interface)

// Declarations
namespace Spartan
{
	struct RHI_Context;
	class RHI_Device;
	class RHI_CommandList;
	class RHI_Pipeline;
	class RHI_SwapChain;
	class RHI_RasterizerState;
	class RHI_BlendState;
	class RHI_DepthStencilState;
	class RHI_InputLayout;
	class RHI_VertexBuffer;
	class RHI_IndexBuffer;
	class RHI_ConstantBuffer;
	class RHI_Sampler;
	class RHI_Viewport;
	class RHI_Texture;
	class RHI_Texture2D;
	class RHI_TextureCube;
	class RHI_Shader;
	struct RHI_Vertex_PosUvNorTan;
	struct RHI_Vertex_PosUvNor;
	struct RHI_Vertex_PosUv;
	struct RHI_Vertex_PosCol;

	enum RHI_Present_Mode
	{
		Present_Immediate,
		Present_Mailbox,
		Present_Fifo,
		Present_Relaxed,
		Present_SharedDemandRefresh,
		Present_SharedDContinuousRefresh
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
		Buffer_Global,
		Buffer_NotAssigned
	};

	enum RHI_PrimitiveTopology_Mode
	{
		PrimitiveTopology_TriangleList,
		PrimitiveTopology_LineList,
		PrimitiveTopology_NotAssigned
	};

	enum RHI_Vertex_Attribute_Type : unsigned long
	{
		Vertex_Attribute_None			= 0,
		Vertex_Attribute_Position2d		= 1UL << 0,
		Vertex_Attribute_Position3d		= 1UL << 1,
		Vertex_Attribute_Color8			= 1UL << 2,
		Vertex_Attribute_Color32		= 1UL << 3,
		Vertex_Attribute_Texture		= 1UL << 4,
		Vertex_Attribute_NormalTangent	= 1UL << 5
	};
	#define Vertex_Attributes_PositionColor					static_cast<RHI_Vertex_Attribute_Type>(Vertex_Attribute_Position3d	| Vertex_Attribute_Color32)
	#define Vertex_Attributes_PositionTexture				static_cast<RHI_Vertex_Attribute_Type>(Vertex_Attribute_Position3d	| Vertex_Attribute_Texture)
	#define Vertex_Attributes_PositionTextureNormalTangent	static_cast<RHI_Vertex_Attribute_Type>(Vertex_Attribute_Position3d	| Vertex_Attribute_Texture | Vertex_Attribute_NormalTangent)
	#define Vertex_Attributes_Position2dTextureColor8		static_cast<RHI_Vertex_Attribute_Type>(Vertex_Attribute_Position2d	| Vertex_Attribute_Texture | Vertex_Attribute_Color8)

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

	enum RHI_Sampler_Address_Mode
	{
		Sampler_Address_Wrap,
		Sampler_Address_Mirror,
		Sampler_Address_Clamp,
		Sampler_Address_Border,
		Sampler_Address_MirrorOnce,
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
		Format_R32_FLOAT_TYPELESS,
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

	enum RHI_Descriptor_Type
	{
		Descriptor_Sampler,
		Descriptor_Texture,
		Descriptor_ConstantBuffer
	};
}