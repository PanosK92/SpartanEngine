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

//= INCLUDES =======================
#include "../../Backends/Backends.h"
//==================================

// RI (Rendering Interface)
namespace Directus
{
	class RI_Texture;
	class RI_Shader;
	class RI_Viewport;
	class RI_Shader;

	struct RI_Vertex_PosUVTBN;
	struct RI_Vertex_PosUVNor;
	struct RI_Vertex_PosUV;
	struct RI_Vertex_PosCol;

	enum PrimitiveTopology
	{
		TriangleList,
		LineList
	};

	enum InputLayout
	{
		Auto,
		Position,
		PositionColor,
		PositionTexture,
		PositionTextureTBN
	};

	enum CullMode
	{
		CullNone,
		CullFront,
		CullBack
	};

	enum FillMode
	{
		FillMode_Solid,
		FillMode_Wireframe
	};

	enum TextureType
	{
		TextureType_Unknown,
		TextureType_Albedo,
		TextureType_Roughness,
		TextureType_Metallic,
		TextureType_Normal,
		TextureType_Height,
		TextureType_Occlusion,
		TextureType_Emission,
		TextureType_Mask,
		TextureType_CubeMap,
	};

	enum Texture_Sampler_Filter
	{
		Texture_Sampler_Point,
		Texture_Sampler_Bilinear,
		Texture_Sampler_Linear,
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
		Texture_Format_R32G32B32A32_FLOAT,
		Texture_Format_R16G16B16A16_FLOAT,
		Texture_Format_R8G8B8A8_UNORM,
		Texture_Format_R8_UNORM
	};
}

#ifdef API_D3D11
// Forward declarations - Graphics API
namespace Directus
{
	class D3D11_Device;
	class D3D11_RenderTexture;
	class D3D11_InputLayout;
	class D3D11_Sampler;
	class D3D11_VertexBuffer;
	class D3D11_IndexBuffer;
	class D3D11_Texture;
	
	typedef D3D11_Device RenderingDevice;
}

// Forward declarations - D3D11 API
struct ID3D11DepthStencilView;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11InputLayout;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11SamplerState;
struct ID3D11Texture2D;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11RenderTargetView;
struct ID3D11BlendState;
struct DXGI_MODE_DESC;
struct IDXGIAdapter;
struct IDXGISwapChain;
struct IDXGIFactory;
struct D3D11_VIEWPORT;
struct D3D11_INPUT_ELEMENT_DESC;
enum D3D_DRIVER_TYPE;
enum D3D_FEATURE_LEVEL;
struct ID3D11Buffer;
struct _D3D_SHADER_MACRO;
typedef _D3D_SHADER_MACRO D3D_SHADER_MACRO;
struct ID3D10Blob;
typedef ID3D10Blob ID3DBlob;
struct ID3DUserDefinedAnnotation;
#endif

#ifdef API_VULKAN
namespace Directus
{
	class Vulkan_Device;

	typedef Vulkan_Device RenderingDevice;
}
#endif
