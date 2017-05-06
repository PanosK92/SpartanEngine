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
	GBuffer::GBuffer(D3D11GraphicsDevice* graphicsDevice)
	{
		m_graphics = graphicsDevice;
		m_depthStencilBuffer = nullptr;
		m_depthStencilView = nullptr;
		m_width = 1;
		m_height = 1;

		// Construct the skeleton of the G-Buffer
		m_renderTargets.push_back(GBufferTex{ DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, nullptr, nullptr }); // albedo
		m_renderTargets.push_back(GBufferTex{ DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, nullptr, nullptr }); // normal
		m_renderTargets.push_back(GBufferTex{ DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, nullptr, nullptr }); // depth
		m_renderTargets.push_back(GBufferTex{ DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, nullptr, nullptr }); // normal
	}

	GBuffer::~GBuffer()
	{
		SafeRelease(m_depthStencilView);
		SafeRelease(m_depthStencilBuffer);

		for (auto& renderTarget : m_renderTargets)
		{
			SafeRelease(renderTarget.shaderResourceView);
			SafeRelease(renderTarget.renderTargetView);
			SafeRelease(renderTarget.renderTexture);
		}
	}

	bool GBuffer::Create(int width, int height)
	{
		if (!m_graphics->GetDevice()) {
			return false;
		}

		// Store the width and height of the render texture.
		m_width = width;
		m_height = height;

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
			textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.SampleDesc.Quality = 0;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			textureDesc.CPUAccessFlags = 0;
			textureDesc.MiscFlags = 0;

			// Create the render target textures.
			HRESULT result = m_graphics->GetDevice()->CreateTexture2D(
				&textureDesc,
				nullptr,
				&renderTarget.renderTexture
			);

			if (FAILED(result)) {
				return false;
			}

			// Setup the description of the render target view.
			D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
			renderTargetViewDesc.Format = textureDesc.Format;
			renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			renderTargetViewDesc.Texture2D.MipSlice = 0;

			// Create the render target views.
			result = m_graphics->GetDevice()->CreateRenderTargetView(
				renderTarget.renderTexture,
				&renderTargetViewDesc,
				&renderTarget.renderTargetView
			);

			if (FAILED(result)) {
				return false;
			}

			// Setup the description of the shader resource view.
			D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
			shaderResourceViewDesc.Format = textureDesc.Format;
			shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
			shaderResourceViewDesc.Texture2D.MipLevels = 1;

			// Create the shader resource views.
			result = m_graphics->GetDevice()->CreateShaderResourceView(
				renderTarget.renderTexture,
				&shaderResourceViewDesc,
				&renderTarget.shaderResourceView
			);

			if (FAILED(result)) {
				return false;
			}
		}

		//= DEPTH =================================================================
		// Initialize the description of the depth buffer.
		D3D11_TEXTURE2D_DESC depthBufferDesc;
		ZeroMemory(&depthBufferDesc, sizeof(depthBufferDesc));

		// Set up the description of the depth buffer.
		depthBufferDesc.Width = width;
		depthBufferDesc.Height = height;
		depthBufferDesc.MipLevels = 1;
		depthBufferDesc.ArraySize = 1;
		depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthBufferDesc.SampleDesc.Count = 1;
		depthBufferDesc.SampleDesc.Quality = 0;
		depthBufferDesc.Usage = D3D11_USAGE_DEFAULT;
		depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		depthBufferDesc.CPUAccessFlags = 0;
		depthBufferDesc.MiscFlags = 0;

		// Create the texture for the depth buffer using the filled out description.
		HRESULT result = m_graphics->GetDevice()->CreateTexture2D(
			&depthBufferDesc,
			nullptr,
			&m_depthStencilBuffer
		);

		if (FAILED(result)) {
			return false;
		}

		// Initailze the depth stencil view description.
		D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
		ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));

		// Set up the depth stencil view description.
		depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		depthStencilViewDesc.Texture2D.MipSlice = 0;

		// Create the depth stencil view.
		result = m_graphics->GetDevice()->CreateDepthStencilView(
			m_depthStencilBuffer,
			&depthStencilViewDesc,
			&m_depthStencilView
		);

		if (FAILED(result)) {
			return false;
		}
		//==================================================================================

		//= VIEWPORT ==================================
		m_viewport.Width = static_cast<float>(width);
		m_viewport.Height = static_cast<float>(height);
		m_viewport.MinDepth = 0.0f;
		m_viewport.MaxDepth = m_maxDepth;
		m_viewport.TopLeftX = 0.0f;
		m_viewport.TopLeftY = 0.0f;
		//=============================================

		return true;
	}

	bool GBuffer::SetAsRenderTarget()
	{
		if (!m_graphics->GetDeviceContext()) {
			return false;
		}

		// Bind the render target view array and depth stencil buffer to the output render pipeline.
		ID3D11RenderTargetView* views[4]{
			m_renderTargets[0].renderTargetView,
			m_renderTargets[1].renderTargetView,
			m_renderTargets[2].renderTargetView,
			m_renderTargets[3].renderTargetView
		};
		ID3D11RenderTargetView** renderTargetViews = views;

		m_graphics->GetDeviceContext()->OMSetRenderTargets(UINT(m_renderTargets.size()), &renderTargetViews[0], m_depthStencilView);

		// Set the viewport.
		m_graphics->GetDeviceContext()->RSSetViewports(1, &m_viewport);

		return true;
	}

	bool GBuffer::Clear(const Vector4& color)
	{
		if (!m_graphics->GetDeviceContext()) {
			return false;
		}

		// Clear the render target buffers.
		for (auto& renderTarget : m_renderTargets) {
			m_graphics->GetDeviceContext()->ClearRenderTargetView(renderTarget.renderTargetView, color.Data());
		}

		// Clear the depth buffer.
		m_graphics->GetDeviceContext()->ClearDepthStencilView(m_depthStencilView, D3D11_CLEAR_DEPTH, m_maxDepth, 0);

		return true;
	}

	bool GBuffer::Clear(float red, float green, float blue, float alpha)
	{
		return Clear(Vector4(red, green, blue, alpha));
	}

	ID3D11ShaderResourceView* GBuffer::GetShaderResourceView(int index)
	{
		if (index < 0 || index > m_renderTargets.size())
			return nullptr;

		return m_renderTargets[index].shaderResourceView;
	}
}