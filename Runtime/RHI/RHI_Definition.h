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
	class RHI_PipelineState;
	class RHI_PipelineCache;
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
	struct RHI_Vertex_Undefined;
	struct RHI_Vertex_PosTex;
	struct RHI_Vertex_PosCol;
	struct RHI_Vertex_PosUvCol;
	struct RHI_Vertex_PosTexNorTan;

	enum RHI_Present_Mode : uint32_t
	{
		Present_Immediate                   = 1 << 0,
		Present_Mailbox                     = 1 << 1,
		Present_Fifo                        = 1 << 2,
		Present_Relaxed                     = 1 << 3,
		Present_SharedDemandRefresh         = 1 << 4,
		Present_SharedDContinuousRefresh    = 1 << 5,

        // Find a way to remove those legacy D3D11 only flags
        Swap_Discard                = 1 << 6,
        Swap_Sequential             = 1 << 7,
        Swap_Flip_Sequential        = 1 << 8,
        Swap_Flip_Discard           = 1 << 9,
        SwapChain_Allow_Mode_Switch = 1 << 10
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

	enum RHI_Filter
	{
		Filter_Nearest,
		Filter_Linear,
	};

	enum RHI_Sampler_Mipmap_Mode
	{
		Sampler_Mipmap_Nearest,
		Sampler_Mipmap_Linear,
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
