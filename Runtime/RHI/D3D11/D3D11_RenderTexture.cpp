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

//= INCLUDES =====================
#include "D3D11_RenderTexture.h"
#include "../../Core/EngineDefs.h"
#include "../../Logging/Log.h"
#include "../IRHI_Implementation.h"
//================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

namespace Directus
{
	D3D11_RenderTexture::D3D11_RenderTexture(D3D11_Device* graphics, int width, int height, bool depth, Texture_Format format)
	{
		m_renderTargetTexture	= nullptr;
		m_renderTargetView		= nullptr;
		m_shaderResourceView	= nullptr;
		m_depthStencilBuffer	= nullptr;
		m_depthStencilView		= nullptr;
		m_graphics				= graphics;
		m_depthEnabled			= depth;
		m_nearPlane				= 0.0f;
		m_farPlane				= 0.0f;
		m_format				= format;
		m_viewport				= IRHI_Viewport((float)width, (float)height, m_graphics->GetMaxDepth());

		Construct();
	}

	D3D11_RenderTexture::~D3D11_RenderTexture()
	{
		SafeRelease(m_depthStencilView);
		SafeRelease(m_depthStencilBuffer);
		SafeRelease(m_shaderResourceView);
		SafeRelease(m_renderTargetView);
		SafeRelease(m_renderTargetTexture);
	}

	bool D3D11_RenderTexture::SetAsRenderTarget()
	{
		if (!m_graphics->GetDeviceContext())
		{
			LOG_INFO("Uninitialized device context. Can't set render texture as rander target");
			return false;
		}

		// Bind the render target view and depth stencil buffer to the output render pipeline.
		m_graphics->GetDeviceContext()->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView);

		// Set the viewport.
		m_graphics->GetDeviceContext()->RSSetViewports(1, (D3D11_VIEWPORT*)&m_viewport);

		return true;
	}

	bool D3D11_RenderTexture::Clear(const Vector4& clearColor)
	{
		if (!m_graphics->GetDeviceContext())
			return false;

		// Clear back buffer
		m_graphics->GetDeviceContext()->ClearRenderTargetView(m_renderTargetView, clearColor.Data()); 

		// Clear depth buffer.
		if (m_depthEnabled)
		{
			m_graphics->GetDeviceContext()->ClearDepthStencilView(m_depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, m_graphics->GetMaxDepth(), 0); 
		}

		return true;
	}

	bool D3D11_RenderTexture::Clear(float red, float green, float blue, float alpha)
	{
		return Clear(Vector4(red, green, blue, alpha));
	}

	void D3D11_RenderTexture::ComputeOrthographicProjectionMatrix(float nearPlane, float farPlane)
	{
		if (m_nearPlane == nearPlane && m_farPlane == farPlane)
			return;

		m_nearPlane = nearPlane;
		m_farPlane = farPlane;
		m_orthographicProjectionMatrix = Matrix::CreateOrthographicLH(m_viewport.GetWidth(), m_viewport.GetHeight(), nearPlane, farPlane);
	}

	bool D3D11_RenderTexture::Construct()
	{
		if (!m_graphics->GetDevice())
		{
			LOG_INFO("D3D11RenderTexture::Construct: Uninitialized device. Can't create render texture");
			return false;
		}

		// RENDER TARGET TEXTURE
		{
			D3D11_TEXTURE2D_DESC textureDesc;
			ZeroMemory(&textureDesc, sizeof(textureDesc));
			textureDesc.Width				= (UINT)m_viewport.GetWidth();
			textureDesc.Height				= (UINT)m_viewport.GetHeight();
			textureDesc.MipLevels			= 1;
			textureDesc.ArraySize			= 1;
			textureDesc.Format				= d3d11_dxgi_format[m_format];
			textureDesc.SampleDesc.Count	= 1;
			textureDesc.SampleDesc.Quality	= 0;
			textureDesc.Usage				= D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags			= D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			textureDesc.CPUAccessFlags		= 0;
			textureDesc.MiscFlags			= 0;

			if (FAILED(m_graphics->GetDevice()->CreateTexture2D(&textureDesc, nullptr, &m_renderTargetTexture)))
			{
				LOG_INFO("D3D11RenderTexture::Construct: CreateTexture2D() failed.");
				return false;
			}
		}

		// RENDER TARGET VIEW
		{
			D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
			renderTargetViewDesc.Format				= d3d11_dxgi_format[m_format];
			renderTargetViewDesc.ViewDimension		= D3D11_RTV_DIMENSION_TEXTURE2D;
			renderTargetViewDesc.Texture2D.MipSlice = 0;

			if (FAILED(m_graphics->GetDevice()->CreateRenderTargetView(m_renderTargetTexture, &renderTargetViewDesc, &m_renderTargetView)))
			{
				LOG_INFO("D3D11RenderTexture::Construct: CreateRenderTargetView() failed.");
				return false;
			}
		}

		// SHADER RESOURCE VIEW
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
			shaderResourceViewDesc.Format						= d3d11_dxgi_format[m_format];
			shaderResourceViewDesc.ViewDimension				= D3D11_SRV_DIMENSION_TEXTURE2D;
			shaderResourceViewDesc.Texture2D.MostDetailedMip	= 0;
			shaderResourceViewDesc.Texture2D.MipLevels			= 1;

			if (FAILED(m_graphics->GetDevice()->CreateShaderResourceView(m_renderTargetTexture, &shaderResourceViewDesc, &m_shaderResourceView)))
			{
				LOG_INFO("D3D11RenderTexture::Construct: CreateShaderResourceView() failed.");
				return false;
			}
		}

		if (!m_depthEnabled)
			return true;

		// DEPTH BUFFER
		{
			D3D11_TEXTURE2D_DESC depthTexDesc;
			ZeroMemory(&depthTexDesc, sizeof(depthTexDesc));
			depthTexDesc.Width				= (UINT)m_viewport.GetWidth();
			depthTexDesc.Height				= (UINT)m_viewport.GetHeight();
			depthTexDesc.MipLevels			= 1;
			depthTexDesc.ArraySize			= 1;
			depthTexDesc.Format				= DXGI_FORMAT_D24_UNORM_S8_UINT;
			depthTexDesc.SampleDesc.Count	= 1;
			depthTexDesc.SampleDesc.Quality	= 0;
			depthTexDesc.Usage				= D3D11_USAGE_DEFAULT;
			depthTexDesc.BindFlags			= D3D11_BIND_DEPTH_STENCIL;
			depthTexDesc.CPUAccessFlags		= 0;
			depthTexDesc.MiscFlags			= 0;

			if (FAILED(m_graphics->GetDevice()->CreateTexture2D(&depthTexDesc, nullptr, &m_depthStencilBuffer)))
			{
				LOG_INFO("D3D11RenderTexture::Construct: CreateTexture2D() failed.");
				return false;
			}
		}

		// DEPTH STENCIL VIEW
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
			ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
			depthStencilViewDesc.Format				= DXGI_FORMAT_D24_UNORM_S8_UINT;
			depthStencilViewDesc.ViewDimension		= D3D11_DSV_DIMENSION_TEXTURE2D;
			depthStencilViewDesc.Texture2D.MipSlice = 0;

			HRESULT result = m_graphics->GetDevice()->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView);
			if (FAILED(result))
			{
				LOG_INFO("D3D11RenderTexture::Construct: CreateDepthStencilView() failed.");
				return false;
			}
		}

		return true;
	}
}