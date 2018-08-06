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

//= INCLUDES ======================
#include "../RHI_Implementation.h"
#include "../RHI_RenderTexture.h"
#include "../RHI_Device.h"
//=================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	RHI_RenderTexture::RHI_RenderTexture(shared_ptr<RHI_Device> rhiDevice, int width, int height, bool depth, Texture_Format format)
	{
		m_renderTargetTexture	= nullptr;
		m_renderTargetView		= nullptr;
		m_shaderResourceView	= nullptr;
		m_depthStencilBuffer	= nullptr;
		m_depthStencilView		= nullptr;
		m_rhiDevice				= rhiDevice;
		m_depthEnabled			= depth;
		m_nearPlane				= 0.0f;
		m_farPlane				= 0.0f;
		m_format				= format;
		m_viewport				= RHI_Viewport((float)width, (float)height, m_rhiDevice->GetViewport().GetMaxDepth());
		m_id					= GENERATE_GUID;

		// RENDER TARGET TEXTURE
		{
			D3D11_TEXTURE2D_DESC textureDesc;
			ZeroMemory(&textureDesc, sizeof(textureDesc));
			textureDesc.Width = (UINT)m_viewport.GetWidth();
			textureDesc.Height = (UINT)m_viewport.GetHeight();
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = d3d11_dxgi_format[m_format];
			textureDesc.SampleDesc.Count = 1;
			textureDesc.SampleDesc.Quality = 0;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			textureDesc.CPUAccessFlags = 0;
			textureDesc.MiscFlags = 0;

			auto ptr = (ID3D11Texture2D**)&m_renderTargetTexture;
			if (FAILED(m_rhiDevice->GetDevice<ID3D11Device>()->CreateTexture2D(&textureDesc, nullptr, ptr)))
			{
				LOG_ERROR("D3D11_RenderTexture::Construct: CreateTexture2D() failed.");
				return;
			}
		}

		// RENDER TARGET VIEW
		{
			D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
			renderTargetViewDesc.Format = d3d11_dxgi_format[m_format];
			renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			renderTargetViewDesc.Texture2D.MipSlice = 0;

			auto ptr = (ID3D11RenderTargetView**)&m_renderTargetView;
			if (FAILED(m_rhiDevice->GetDevice<ID3D11Device>()->CreateRenderTargetView((ID3D11Resource*)m_renderTargetTexture, &renderTargetViewDesc, ptr)))
			{
				LOG_ERROR("D3D11_RenderTexture::Construct: CreateRenderTargetView() failed.");
				return;
			}
		}

		// SHADER RESOURCE VIEW
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
			shaderResourceViewDesc.Format = d3d11_dxgi_format[m_format];
			shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
			shaderResourceViewDesc.Texture2D.MipLevels = 1;

			auto ptr = (ID3D11ShaderResourceView**)&m_shaderResourceView;
			if (FAILED(m_rhiDevice->GetDevice<ID3D11Device>()->CreateShaderResourceView((ID3D11Texture2D*)m_renderTargetTexture, &shaderResourceViewDesc, ptr)))
			{
				LOG_ERROR("D3D11_RenderTexture::Construct: CreateShaderResourceView() failed.");
				return;
			}
		}

		if (!m_depthEnabled)
			return;

		// DEPTH BUFFER
		{
			D3D11_TEXTURE2D_DESC depthTexDesc;
			ZeroMemory(&depthTexDesc, sizeof(depthTexDesc));
			depthTexDesc.Width = (UINT)m_viewport.GetWidth();
			depthTexDesc.Height = (UINT)m_viewport.GetHeight();
			depthTexDesc.MipLevels = 1;
			depthTexDesc.ArraySize = 1;
			depthTexDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			depthTexDesc.SampleDesc.Count = 1;
			depthTexDesc.SampleDesc.Quality = 0;
			depthTexDesc.Usage = D3D11_USAGE_DEFAULT;
			depthTexDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			depthTexDesc.CPUAccessFlags = 0;
			depthTexDesc.MiscFlags = 0;

			auto ptr = (ID3D11Texture2D**)&m_depthStencilBuffer;
			if (FAILED(m_rhiDevice->GetDevice<ID3D11Device>()->CreateTexture2D(&depthTexDesc, nullptr, ptr)))
			{
				LOG_ERROR("D3D11_RenderTexture::Construct: CreateTexture2D() failed.");
				return;
			}
		}

		// DEPTH STENCIL VIEW
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
			ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
			depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			depthStencilViewDesc.Texture2D.MipSlice = 0;

			auto ptr = (ID3D11DepthStencilView**)&m_depthStencilView;
			auto result = m_rhiDevice->GetDevice<ID3D11Device>()->CreateDepthStencilView((ID3D11Texture2D*)m_depthStencilBuffer, &depthStencilViewDesc, ptr);
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_RenderTexture::Construct: CreateDepthStencilView() failed.");
				return;
			}
		}
	}

	RHI_RenderTexture::~RHI_RenderTexture()
	{
		SafeRelease((ID3D11Texture2D*)m_renderTargetTexture);
		SafeRelease((ID3D11RenderTargetView*)m_renderTargetView);
		SafeRelease((ID3D11ShaderResourceView*)m_shaderResourceView);
		SafeRelease((ID3D11Texture2D*)m_depthStencilBuffer);
		SafeRelease((ID3D11DepthStencilView*)m_depthStencilView);
	}

	bool RHI_RenderTexture::Clear(const Vector4& clearColor)
	{
		if (!m_rhiDevice)
			return false;

		// Clear back buffer
		m_rhiDevice->ClearRenderTarget(m_renderTargetView, clearColor); 

		// Clear depth buffer.
		if (m_depthEnabled)
		{
			float maxDepth	= m_rhiDevice->GetViewport().GetMaxDepth();
			uint8_t stencil = 0;
			m_rhiDevice->ClearDepthStencil(m_depthStencilView, Clear_Depth | Clear_Stencil, maxDepth, stencil); 
		}

		return true;
	}

	bool RHI_RenderTexture::Clear(float red, float green, float blue, float alpha)
	{
		return Clear(Vector4(red, green, blue, alpha));
	}

	void RHI_RenderTexture::ComputeOrthographicProjectionMatrix(float nearPlane, float farPlane)
	{
		if (m_nearPlane == nearPlane && m_farPlane == farPlane)
			return;

		m_nearPlane						= nearPlane;
		m_farPlane						= farPlane;
		m_orthographicProjectionMatrix	= Matrix::CreateOrthographicLH(m_viewport.GetWidth(), m_viewport.GetHeight(), nearPlane, farPlane);
	}

	void* RHI_RenderTexture::GetRenderTarget()
	{
		return m_renderTargetView;
	}

	void* RHI_RenderTexture::GetShaderResource()
	{
		return m_shaderResourceView;
	}

	void* RHI_RenderTexture::GetDepthStencil()
	{
		return m_depthStencilView;
	}
}