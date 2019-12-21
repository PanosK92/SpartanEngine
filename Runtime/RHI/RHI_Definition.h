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
		RHI_Present_Immediate                   = 1 << 0,
		RHI_Present_Mailbox                     = 1 << 1,
		RHI_Present_Fifo                        = 1 << 2,
		RHI_Present_Relaxed                     = 1 << 3,
		RHI_Present_SharedDemandRefresh         = 1 << 4,
		RHI_Present_SharedDContinuousRefresh    = 1 << 5,

        // Find a way to remove those legacy D3D11 only flags
        RHI_Swap_Discard                = 1 << 6,
        RHI_Swap_Sequential             = 1 << 7,
        RHI_Swap_Flip_Sequential        = 1 << 8,
        RHI_Swap_Flip_Discard           = 1 << 9,
        RHI_SwapChain_Allow_Mode_Switch = 1 << 10
	};

	enum RHI_Query_Type
	{
		RHI_Query_Timestamp,
		RHI_Query_Timestamp_Disjoint
	};

	enum RHI_Clear_Buffer
	{
		RHI_Clear_Depth		= 1 << 0,
		RHI_Clear_Stencil	= 1 << 1
	};

	enum RHI_Buffer_Scope : uint8_t
	{
		RHI_Buffer_VertexShader = 1 << 0,
		RHI_Buffer_PixelShader  = 1 << 1,
	};

	enum RHI_PrimitiveTopology_Mode
	{
		RHI_PrimitiveTopology_TriangleList,
		RHI_PrimitiveTopology_LineList,
		RHI_PrimitiveTopology_Unknown
	};

	enum RHI_Cull_Mode
	{
		RHI_Cull_None,
		RHI_Cull_Front,
		RHI_Cull_Back
	};

	enum RHI_Fill_Mode
	{
		RHI_Fill_Solid,
		RHI_Fill_Wireframe
	};

	enum RHI_Filter
	{
		RHI_Filter_Nearest,
		RHI_Filter_Linear,
	};

	enum RHI_Sampler_Mipmap_Mode
	{
		RHI_Sampler_Mipmap_Nearest,
		RHI_Sampler_Mipmap_Linear,
	};

	enum RHI_Sampler_Address_Mode
	{
		RHI_Sampler_Address_Wrap,
		RHI_Sampler_Address_Mirror,
		RHI_Sampler_Address_Clamp,
		RHI_Sampler_Address_Border,
		RHI_Sampler_Address_MirrorOnce,
	};

	enum RHI_Comparison_Function
	{
		RHI_Comparison_Never,
		RHI_Comparison_Less,
		RHI_Comparison_Equal,
		RHI_Comparison_LessEqual,
		RHI_Comparison_Greater,
		RHI_Comparison_NotEqual,
		RHI_Comparison_GreaterEqual,
		RHI_Comparison_Always
	};

	enum RHI_Format
	{
		// R
		RHI_Format_R8_Unorm,
		RHI_Format_R16_Uint,
		RHI_Format_R16_Float,
		RHI_Format_R32_Uint,
		RHI_Format_R32_Float,
		RHI_Format_D32_Float,
		RHI_Format_R32_Float_Typeless,
		// RG
		RHI_Format_R8G8_Unorm,
		RHI_Format_R16G16_Float,
		RHI_Format_R32G32_Float,	
		// RGB
		RHI_Format_R32G32B32_Float,
		// RGBA
		RHI_Format_R8G8B8A8_Unorm,
		RHI_Format_R16G16B16A16_Float,
		RHI_Format_R32G32B32A32_Float
	};

	enum RHI_Blend
	{
		RHI_Blend_Zero,
		RHI_Blend_One,
		RHI_Blend_Src_Color,
		RHI_Blend_Inv_Src_Color,
		RHI_Blend_Src_Alpha,
		RHI_Blend_Inv_Src_Alpha
	};

	enum RHI_Blend_Operation
	{
		RHI_Blend_Operation_Add,
		RHI_Blend_Operation_Subtract,
		RHI_Blend_Operation_Rev_Subtract,
		RHI_Blend_Operation_Min,
		RHI_Blend_Operation_Max
	};

	enum RHI_Descriptor_Type
	{
		RHI_Descriptor_Sampler,
		RHI_Descriptor_Texture,
		RHI_Descriptor_ConstantBuffer,
        RHI_Descriptor_Undefined
	};

    enum RHI_Image_Layout
    {
        RHI_Image_Undefined,
        RHI_Image_General,
        RHI_Image_Preinitialized,
        RHI_Image_Color_Attachment_Optimal,
        RHI_Image_Depth_Stencil_Attachment_Optimal,
        RHI_Image_Depth_Stencil_Read_Only_Optimal,    
        RHI_Image_Shader_Read_Only_Optimal,
        RHI_Image_Transfer_Dst_Optimal,
        RHI_Image_Present_Src
    };

    struct RHI_Descriptor
    {
        RHI_Descriptor() = default;

        RHI_Descriptor(const RHI_Descriptor& descriptor)
        {
            type    = descriptor.type;
            slot    = descriptor.slot;
            stage   = descriptor.stage;
        }

        RHI_Descriptor(RHI_Descriptor_Type type, uint32_t slot, uint32_t stage)
        {
            this->type  = type;
            this->slot  = slot;
            this->stage = stage;
        }

        
        uint32_t slot               = 0;
        uint32_t stage              = 0;
        uint32_t id                 = 0;
        uint64_t size               = 0;
        RHI_Descriptor_Type type    = RHI_Descriptor_Undefined;
        RHI_Image_Layout layout     = RHI_Image_Undefined;
        void* resource              = nullptr;

    };

    inline const char* rhi_format_to_string(const RHI_Format result)
    {
        switch (result)
        {
        case RHI_Format_R8_Unorm:           return "RHI_Format_R8_Unorm";
        case RHI_Format_R16_Uint:			return "RHI_Format_R16_Uint";
        case RHI_Format_R16_Float:			return "RHI_Format_R16_Float";
        case RHI_Format_R32_Uint:			return "RHI_Format_R32_Uint";
        case RHI_Format_R32_Float:			return "RHI_Format_R32_Float";
        case RHI_Format_D32_Float:			return "RHI_Format_D32_Float";
        case RHI_Format_R32_Float_Typeless: return "RHI_Format_R32_Float_Typeless";
        case RHI_Format_R8G8_Unorm:			return "RHI_Format_R8G8_Unorm";
        case RHI_Format_R16G16_Float:		return "RHI_Format_R16G16_Float";
        case RHI_Format_R32G32_Float:		return "RHI_Format_R32G32_Float";
        case RHI_Format_R32G32B32_Float:	return "RHI_Format_R32G32B32_Float";
        case RHI_Format_R8G8B8A8_Unorm:		return "RHI_Format_R8G8B8A8_Unorm";
        case RHI_Format_R16G16B16A16_Float:	return "RHI_Format_R16G16B16A16_Float";
        case RHI_Format_R32G32B32A32_Float:	return "RHI_Format_R32G32B32A32_Float";
        }

        return "Unknown format";
    }
}
