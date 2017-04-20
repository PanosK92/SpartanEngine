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

//= INCLUDES ===================
#include "D3D11RenderTexture.h"
#include "D3D11GraphicsDevice.h"
#include "../../Core/Settings.h"
#include "../../Core/Helper.h"
//==============================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

D3D11RenderTexture::D3D11RenderTexture(D3D11GraphicsDevice* graphicsDevice) : m_graphics(graphicsDevice)
{
	m_renderTargetTexture = nullptr;
	m_renderTargetView = nullptr;
	m_shaderResourceView = nullptr;
	m_depthStencilBuffer = nullptr;
	m_depthStencilView = nullptr;
	m_width = RESOLUTION_WIDTH;
	m_height = RESOLUTION_HEIGHT;
}

D3D11RenderTexture::~D3D11RenderTexture()
{
	SafeRelease(m_depthStencilView);
	SafeRelease(m_depthStencilBuffer);
	SafeRelease(m_shaderResourceView);
	SafeRelease(m_renderTargetView);
	SafeRelease(m_renderTargetTexture);
}

bool D3D11RenderTexture::Create(int width, int height)
{
	if (!m_graphics->GetDevice()) {
		return false;
	}

	m_width = height;
	m_height = height;

	//= RENDER TARGET TEXTURE ========================================================================================================
	D3D11_TEXTURE2D_DESC textureDesc;
	ZeroMemory(&textureDesc, sizeof(textureDesc));
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

	HRESULT result = m_graphics->GetDevice()->CreateTexture2D(&textureDesc, nullptr, &m_renderTargetTexture);
	if (FAILED(result)) {
		return false;
	}
	//=================================================================================================================================

	//= RENDER TARGET VIEW ============================================================================================================
	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
	renderTargetViewDesc.Format = textureDesc.Format;
	renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	renderTargetViewDesc.Texture2D.MipSlice = 0;

	result = m_graphics->GetDevice()->CreateRenderTargetView(m_renderTargetTexture, &renderTargetViewDesc, &m_renderTargetView);
	if (FAILED(result))
		return false;
	//=================================================================================================================================

	//= SHADER RESOURCE VIEW ==========================================================================================================
	D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
	shaderResourceViewDesc.Format = textureDesc.Format;
	shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	result = m_graphics->GetDevice()->CreateShaderResourceView(m_renderTargetTexture, &shaderResourceViewDesc, &m_shaderResourceView);
	if (FAILED(result))
		return false;
	//=================================================================================================================================

	//= VIEWPORT =====================
	m_viewport.Width = (FLOAT)width;
	m_viewport.Height = (FLOAT)height;
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = m_maxDepth;
	m_viewport.TopLeftX = 0.0f;
	m_viewport.TopLeftY = 0.0f;
	//================================

	//= DEPTH BUFFER ==================================================================================================================
	D3D11_TEXTURE2D_DESC depthBufferDesc;
	ZeroMemory(&depthBufferDesc, sizeof(depthBufferDesc));
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

	result = m_graphics->GetDevice()->CreateTexture2D(&depthBufferDesc, nullptr, &m_depthStencilBuffer);
	if (FAILED(result))
		return false;
	//=================================================================================================================================

	//= DEPTH STENCIL VIEW ============================================================================================================
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	result = m_graphics->GetDevice()->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView);
	if (FAILED(result))
		return false;
	//=================================================================================================================================

	return true;
}

bool D3D11RenderTexture::SetAsRenderTarget()
{
	if (!m_graphics->GetDeviceContext()) {
		return false;
	}

	// Bind the render target view and depth stencil buffer to the output render pipeline.
	m_graphics->GetDeviceContext()->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView);

	// Set the viewport.
	m_graphics->GetDeviceContext()->RSSetViewports(1, &m_viewport);

	return true;
}

bool D3D11RenderTexture::Clear(const Vector4& clearColor)
{
	if (!m_graphics->GetDeviceContext()) {
		return false;
	}

	m_graphics->GetDeviceContext()->ClearRenderTargetView(m_renderTargetView, clearColor.Data()); // Clear back buffer.
	m_graphics->GetDeviceContext()->ClearDepthStencilView(m_depthStencilView, D3D11_CLEAR_DEPTH, m_maxDepth, 0); // Clear depth buffer.

	return true;
}

bool D3D11RenderTexture::Clear(float red, float green, float blue, float alpha)
{
	return Clear(Vector4(red, green, blue, alpha));
}

void D3D11RenderTexture::CalculateOrthographicProjectionMatrix(float nearPlane, float farPlane)
{
	if (m_nearPlane == nearPlane && m_farPlane == farPlane) {
		return;
	}

	m_nearPlane = nearPlane;
	m_farPlane = farPlane;
	m_orthographicProjectionMatrix = Matrix::CreateOrthographicLH(float(m_width), float(m_height), nearPlane, farPlane);
}