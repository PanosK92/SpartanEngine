/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ===============
#include "GBuffer.h"
#include "../Core/Helper.h"
//==========================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

namespace Directus
{
	GBuffer::GBuffer(Graphics* graphics)
	{
		m_graphics = graphics;

		// Construct the skeleton of the G-Buffer
		m_renderTargets.push_back(GBufferTex{ DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, nullptr, nullptr }); // albedo
		m_renderTargets.push_back(GBufferTex{ DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, nullptr, nullptr }); // normal
		m_renderTargets.push_back(GBufferTex{ DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, nullptr, nullptr }); // depth
		m_renderTargets.push_back(GBufferTex{ DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, nullptr, nullptr }); // material
	}

	GBuffer::~GBuffer()
	{
		for (auto& renderTarget : m_renderTargets)
		{
			SafeRelease(renderTarget.shaderResourceView);
			SafeRelease(renderTarget.renderTargetView);
			SafeRelease(renderTarget.renderTexture);
		}
	}

	bool GBuffer::Create(int width, int height)
	{
		if (!m_graphics)
			return false;

		if (!m_graphics->GetDevice())
			return false;

		for (auto& renderTarget : m_renderTargets)
		{
			// Initialize the render target texture description.
			D3D11_TEXTURE2D_DESC textureDesc;
			ZeroMemory(&textureDesc, sizeof(textureDesc));

			// Setup the render target texture description.
			textureDesc.Width = width;
			textureDesc.Height = height;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = renderTarget.format;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.SampleDesc.Quality = 0;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			textureDesc.CPUAccessFlags = 0;
			textureDesc.MiscFlags = 0;

			// Create the render target textures.
			if (FAILED(m_graphics->GetDevice()->CreateTexture2D(&textureDesc, nullptr, &renderTarget.renderTexture)))
				return false;

			// Setup the description of the render target view.
			D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
			renderTargetViewDesc.Format = textureDesc.Format;
			renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			renderTargetViewDesc.Texture2D.MipSlice = 0;

			// Create the render target views.
			if (FAILED(m_graphics->GetDevice()->CreateRenderTargetView(renderTarget.renderTexture, &renderTargetViewDesc, &renderTarget.renderTargetView)))
				return false;

			// Setup the description of the shader resource view.
			D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
			shaderResourceViewDesc.Format = textureDesc.Format;
			shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
			shaderResourceViewDesc.Texture2D.MipLevels = 1;

			// Create the shader resource views.
			if (FAILED(m_graphics->GetDevice()->CreateShaderResourceView(renderTarget.renderTexture, &shaderResourceViewDesc, &renderTarget.shaderResourceView)))
				return false;
		}	

		return true;
	}

	bool GBuffer::SetAsRenderTarget()
	{
		if (!m_graphics->GetDeviceContext())
			return false;

		// Bind the render target view array and depth stencil buffer to the output render pipeline.
		ID3D11RenderTargetView* views[4]
		{
			m_renderTargets[0].renderTargetView,
			m_renderTargets[1].renderTargetView,
			m_renderTargets[2].renderTargetView,
			m_renderTargets[3].renderTargetView
		};
		ID3D11RenderTargetView** renderTargetViews = views;

		m_graphics->GetDeviceContext()->OMSetRenderTargets(UINT(m_renderTargets.size()), &renderTargetViews[0], m_graphics->GetDepthStencilView());

		return true;
	}

	bool GBuffer::Clear()
	{
		if (!m_graphics->GetDeviceContext())
			return false;

		// Clear the render target buffers.
		for (auto& renderTarget : m_renderTargets)
		{
			m_graphics->GetDeviceContext()->ClearRenderTargetView(renderTarget.renderTargetView, Vector4(0,0,0,0).Data());
		}

		// Clear the depth buffer.
		m_graphics->GetDeviceContext()->ClearDepthStencilView(m_graphics->GetDepthStencilView(), D3D11_CLEAR_DEPTH, m_graphics->GetMaxDepth(), 0);

		return true;
	}

	ID3D11ShaderResourceView* GBuffer::GetShaderResource(int index)
	{
		if (index < 0 || index > m_renderTargets.size())
			return nullptr;

		return m_renderTargets[index].shaderResourceView;
	}
}