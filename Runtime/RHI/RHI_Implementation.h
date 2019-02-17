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

#ifdef ENGINE_RUNTIME // Only compile this for the engine

//#ifdef API_D3D11 // Deactivate this for now as Vulkan is not ready
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")	
#include <d3d11_4.h>

static const DXGI_SWAP_EFFECT d3d11_swap_effect[] =
{
	DXGI_SWAP_EFFECT_DISCARD,
	DXGI_SWAP_EFFECT_SEQUENTIAL,
	DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
	DXGI_SWAP_EFFECT_FLIP_DISCARD
};

static const D3D11_CULL_MODE d3d11_cull_mode[] =
{
	D3D11_CULL_NONE,
	D3D11_CULL_FRONT,
	D3D11_CULL_BACK
};

static const D3D11_FILL_MODE d3d11_fill_Mode[] =
{
	D3D11_FILL_SOLID,
	D3D11_FILL_WIREFRAME
};

static const D3D11_PRIMITIVE_TOPOLOGY d3d11_primitive_topology[] =
{
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
	D3D11_PRIMITIVE_TOPOLOGY_LINELIST
};

static const DXGI_FORMAT d3d11_dxgi_format[] =
{
	DXGI_FORMAT_R8_UNORM,
	DXGI_FORMAT_R16_UINT,
	DXGI_FORMAT_R16_FLOAT,
	DXGI_FORMAT_R32_UINT,
	DXGI_FORMAT_R32_FLOAT,
	DXGI_FORMAT_D32_FLOAT,

	DXGI_FORMAT_R8G8_UNORM,
	DXGI_FORMAT_R16G16_FLOAT,
	DXGI_FORMAT_R32G32_FLOAT,
	
	DXGI_FORMAT_R32G32B32_FLOAT,

	DXGI_FORMAT_R8G8B8A8_UNORM,
	DXGI_FORMAT_R16G16B16A16_FLOAT,
	DXGI_FORMAT_R32G32B32A32_FLOAT
};

static const D3D11_TEXTURE_ADDRESS_MODE d3d11_texture_address_mode[] =
{
	D3D11_TEXTURE_ADDRESS_WRAP,
	D3D11_TEXTURE_ADDRESS_MIRROR,
	D3D11_TEXTURE_ADDRESS_CLAMP,
	D3D11_TEXTURE_ADDRESS_BORDER,
	D3D11_TEXTURE_ADDRESS_MIRROR_ONCE
};

static const D3D11_COMPARISON_FUNC d3d11_comparison_func[] =
{
	D3D11_COMPARISON_NEVER,
	D3D11_COMPARISON_LESS,
	D3D11_COMPARISON_EQUAL,
	D3D11_COMPARISON_LESS_EQUAL,
	D3D11_COMPARISON_GREATER,
	D3D11_COMPARISON_NOT_EQUAL,
	D3D11_COMPARISON_GREATER_EQUAL,
	D3D11_COMPARISON_ALWAYS
};

static const D3D11_FILTER d3d11_filter[] =
{
	D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
	D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
	D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
	D3D11_FILTER_MIN_MAG_MIP_POINT,
	D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,	// Use linear interpolation for minification and magnification, use point sampling for mip-level sampling.
	D3D11_FILTER_MIN_MAG_MIP_LINEAR,		// Use linear interpolation for minification, magnification, and mip-level sampling.
	D3D11_FILTER_ANISOTROPIC
};

static const D3D11_BLEND d3d11_blend[] =
{
	D3D11_BLEND_ZERO,
	D3D11_BLEND_ONE,
	D3D11_BLEND_SRC_COLOR,
	D3D11_BLEND_INV_SRC_COLOR,
	D3D11_BLEND_SRC_ALPHA,
	D3D11_BLEND_INV_SRC_ALPHA
};

static const D3D11_BLEND_OP d3d11_blend_op[] =
{
	D3D11_BLEND_OP_ADD,
	D3D11_BLEND_OP_SUBTRACT,
	D3D11_BLEND_OP_REV_SUBTRACT,
	D3D11_BLEND_OP_MIN,
	D3D11_BLEND_OP_MAX
};
//#endif

#ifdef API_VULKAN
#pragma comment(lib, "vulkan-1.lib")
#endif

#endif