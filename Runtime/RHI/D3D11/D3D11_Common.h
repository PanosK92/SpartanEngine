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

inline D3D11_DEPTH_STENCIL_DESC Desc_DepthDisabled()
{
	D3D11_DEPTH_STENCIL_DESC dsDesc;
	dsDesc.DepthEnable						= false;
	dsDesc.DepthWriteMask					= D3D11_DEPTH_WRITE_MASK_ZERO;
	dsDesc.DepthFunc						= D3D11_COMPARISON_LESS_EQUAL;
	dsDesc.StencilEnable					= false;
	dsDesc.StencilReadMask					= D3D11_DEFAULT_STENCIL_READ_MASK;
	dsDesc.StencilWriteMask					= D3D11_DEFAULT_STENCIL_WRITE_MASK;
	dsDesc.FrontFace.StencilDepthFailOp		= D3D11_STENCIL_OP_KEEP;
	dsDesc.FrontFace.StencilFailOp			= D3D11_STENCIL_OP_KEEP;
	dsDesc.FrontFace.StencilPassOp			= D3D11_STENCIL_OP_REPLACE;
	dsDesc.FrontFace.StencilFunc			= D3D11_COMPARISON_ALWAYS;
	dsDesc.BackFace							= dsDesc.FrontFace;

	return dsDesc;
}

inline D3D11_DEPTH_STENCIL_DESC Desc_DepthEnabled()
{
	D3D11_DEPTH_STENCIL_DESC desc;
	desc.DepthEnable					= true;
	desc.DepthWriteMask					= D3D11_DEPTH_WRITE_MASK_ALL;
	desc.DepthFunc						= D3D11_COMPARISON_LESS_EQUAL;
	desc.StencilEnable					= false;
	desc.StencilReadMask				= D3D11_DEFAULT_STENCIL_READ_MASK;
	desc.StencilWriteMask				= D3D11_DEFAULT_STENCIL_WRITE_MASK;
	desc.FrontFace.StencilDepthFailOp	= D3D11_STENCIL_OP_KEEP;
	desc.FrontFace.StencilFailOp		= D3D11_STENCIL_OP_KEEP;
	desc.FrontFace.StencilPassOp		= D3D11_STENCIL_OP_REPLACE;
	desc.FrontFace.StencilFunc			= D3D11_COMPARISON_ALWAYS;
	desc.BackFace						= desc.FrontFace;

	return desc;
}

inline D3D11_DEPTH_STENCIL_DESC Desc_DepthReverseEnabled()
{
	D3D11_DEPTH_STENCIL_DESC dsDesc;
	dsDesc.DepthEnable					= true;
	dsDesc.DepthWriteMask				= D3D11_DEPTH_WRITE_MASK_ALL;
	dsDesc.DepthFunc					= D3D11_COMPARISON_GREATER_EQUAL;
	dsDesc.StencilEnable				= false;
	dsDesc.StencilReadMask				= D3D11_DEFAULT_STENCIL_READ_MASK;
	dsDesc.StencilWriteMask				= D3D11_DEFAULT_STENCIL_WRITE_MASK;
	dsDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	dsDesc.FrontFace.StencilFailOp		= D3D11_STENCIL_OP_KEEP;
	dsDesc.FrontFace.StencilPassOp		= D3D11_STENCIL_OP_REPLACE;
	dsDesc.FrontFace.StencilFunc		= D3D11_COMPARISON_ALWAYS;
	dsDesc.BackFace						= dsDesc.FrontFace;

	return dsDesc;
}

inline D3D11_RASTERIZER_DESC Desc_RasterizerCullNone()
{
	D3D11_RASTERIZER_DESC rastDesc;

	rastDesc.AntialiasedLineEnable		= true;
	rastDesc.CullMode					= D3D11_CULL_NONE;
	rastDesc.DepthBias					= 0;
	rastDesc.DepthBiasClamp				= 0.0f;
	rastDesc.DepthClipEnable			= true;
	rastDesc.FillMode					= D3D11_FILL_SOLID;
	rastDesc.FrontCounterClockwise		= false;
	rastDesc.MultisampleEnable			= false;
	rastDesc.ScissorEnable				= false;
	rastDesc.SlopeScaledDepthBias		= 0;

	return rastDesc;
}

inline D3D11_RASTERIZER_DESC Desc_RasterizerCullFront()
{
	D3D11_RASTERIZER_DESC rastDesc;

	rastDesc.AntialiasedLineEnable	= true;
	rastDesc.CullMode				= D3D11_CULL_FRONT;
	rastDesc.DepthBias				= 0;
	rastDesc.DepthBiasClamp			= 0.0f;
	rastDesc.DepthClipEnable		= true;
	rastDesc.FillMode				= D3D11_FILL_SOLID;
	rastDesc.FrontCounterClockwise	= false;
	rastDesc.MultisampleEnable		= false;
	rastDesc.ScissorEnable			= false;
	rastDesc.SlopeScaledDepthBias	= 0;

	return rastDesc;
}

inline D3D11_RASTERIZER_DESC Desc_RasterizerCullBack()
{
	D3D11_RASTERIZER_DESC rastDesc;

	rastDesc.AntialiasedLineEnable	= true;
	rastDesc.CullMode				= D3D11_CULL_BACK;
	rastDesc.DepthBias				= 0;
	rastDesc.DepthBiasClamp			= 0.0f;
	rastDesc.DepthClipEnable		= true;
	rastDesc.FillMode				= D3D11_FILL_SOLID;
	rastDesc.FrontCounterClockwise	= false;
	rastDesc.MultisampleEnable		= false;
	rastDesc.ScissorEnable			= false;
	rastDesc.SlopeScaledDepthBias	= 0;

	return rastDesc;
}


inline D3D11_BLEND_DESC Desc_BlendDisabled()
{
	D3D11_BLEND_DESC blendDesc;
	blendDesc.AlphaToCoverageEnable		= false;
	blendDesc.IndependentBlendEnable	= false;
	for (UINT i = 0; i < 8; ++i)
	{
		blendDesc.RenderTarget[i].BlendEnable			= false;
		blendDesc.RenderTarget[i].BlendOp				= D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[i].BlendOpAlpha			= D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[i].DestBlend				= D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[i].DestBlendAlpha		= D3D11_BLEND_ONE;
		blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		blendDesc.RenderTarget[i].SrcBlend				= D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[i].SrcBlendAlpha			= D3D11_BLEND_ONE;
	}

	return blendDesc;
}

inline D3D11_BLEND_DESC Desc_BlendAlpha()
{
	D3D11_BLEND_DESC blendDesc;
	blendDesc.AlphaToCoverageEnable		= false;
	blendDesc.IndependentBlendEnable	= true;
	for (UINT i = 0; i < 8; ++i)
	{
		blendDesc.RenderTarget[i].BlendEnable			= true;
		blendDesc.RenderTarget[i].BlendOp				= D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[i].BlendOpAlpha			= D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[i].DestBlend				= D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[i].DestBlendAlpha		= D3D11_BLEND_ONE;
		blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		blendDesc.RenderTarget[i].SrcBlend				= D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[i].SrcBlendAlpha			= D3D11_BLEND_ONE;
	}

	blendDesc.RenderTarget[0].BlendEnable = true;

	return blendDesc;
}

inline D3D11_BLEND_DESC Desc_BlendColorWriteDisabled()
{
	D3D11_BLEND_DESC blendDesc;
	blendDesc.AlphaToCoverageEnable		= false;
	blendDesc.IndependentBlendEnable	= false;
	for (UINT i = 0; i < 8; ++i)
	{
		blendDesc.RenderTarget[i].BlendEnable			= false;
		blendDesc.RenderTarget[i].BlendOp				= D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[i].BlendOpAlpha			= D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[i].DestBlend				= D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[i].DestBlendAlpha		= D3D11_BLEND_ONE;
		blendDesc.RenderTarget[i].RenderTargetWriteMask = 0;
		blendDesc.RenderTarget[i].SrcBlend				= D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[i].SrcBlendAlpha			= D3D11_BLEND_ONE;
	}

	return blendDesc;
}