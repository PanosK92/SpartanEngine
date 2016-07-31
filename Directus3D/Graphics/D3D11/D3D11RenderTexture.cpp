/*
Copyright(c) 2016 Panos Karabelas

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
#include "D3D11Device.h"
#include "../../Core/Settings.h"
#include "../../Core/Globals.h"
//==============================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

D3D11RenderTexture::D3D11RenderTexture()
{
	m_graphicsDevice = nullptr;
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

bool D3D11RenderTexture::Initialize(GraphicsDevice* graphicsDevice, int textureWidth, int textureHeight)
{
	m_graphicsDevice = graphicsDevice;
	m_width = textureHeight;
	m_height = textureHeight;

	// Initialize the render target texture description.
	D3D11_TEXTURE2D_DESC textureDesc;
	ZeroMemory(&textureDesc, sizeof(textureDesc));

	// Setup the render target texture description.
	textureDesc.Width = textureWidth;
	textureDesc.Height = textureHeight;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;

	// Create the render target texture.
	HRESULT result = m_graphicsDevice->GetDevice()->CreateTexture2D(&textureDesc, nullptr, &m_renderTargetTexture);
	if (FAILED(result))
		return false;

	// Setup the description of the render target view.
	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
	renderTargetViewDesc.Format = textureDesc.Format;
	renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	renderTargetViewDesc.Texture2D.MipSlice = 0;

	// Create the render target view.
	result = m_graphicsDevice->GetDevice()->CreateRenderTargetView(m_renderTargetTexture, &renderTargetViewDesc, &m_renderTargetView);
	if (FAILED(result))
		return false;

	// Setup the description of the shader resource view.
	D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
	shaderResourceViewDesc.Format = textureDesc.Format;
	shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	// Create the shader resource view.
	result = m_graphicsDevice->GetDevice()->CreateShaderResourceView(m_renderTargetTexture, &shaderResourceViewDesc, &m_shaderResourceView);
	if (FAILED(result))
		return false;

	D3D11_TEXTURE2D_DESC depthBufferDesc;
	// Initialize the description of the depth buffer.
	ZeroMemory(&depthBufferDesc, sizeof(depthBufferDesc));
	// Set up the description of the depth buffer.
	depthBufferDesc.Width = textureWidth;
	depthBufferDesc.Height = textureHeight;
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
	result = m_graphicsDevice->GetDevice()->CreateTexture2D(&depthBufferDesc, nullptr, &m_depthStencilBuffer);
	if (FAILED(result))
		return false;

	// Initailze the depth stencil view description.
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	// Create the depth stencil view.
	result = m_graphicsDevice->GetDevice()->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView);
	if (FAILED(result))
		return false;

	// Setup the viewport for rendering.
	m_viewport.Width = float(textureWidth);
	m_viewport.Height = float(textureHeight);
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = 1.0f;
	m_viewport.TopLeftX = 0.0f;
	m_viewport.TopLeftY = 0.0f;

	return true;
}

void D3D11RenderTexture::SetAsRenderTarget()
{
	// Bind the render target view and depth stencil buffer to the output render pipeline.
	m_graphicsDevice->GetDeviceContext()->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView);

	// Set the viewport.
	m_graphicsDevice->GetDeviceContext()->RSSetViewports(1, &m_viewport);
}

void D3D11RenderTexture::Clear(float red, float green, float blue, float alpha)
{
	float clearColor[4];

	// Setup the color to clear the buffer to.
	clearColor[0] = red;
	clearColor[1] = green;
	clearColor[2] = blue;
	clearColor[3] = alpha;

	m_graphicsDevice->GetDeviceContext()->ClearRenderTargetView(m_renderTargetView, clearColor); // Clear the back buffer.
	m_graphicsDevice->GetDeviceContext()->ClearDepthStencilView(m_depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0); // Clear the depth buffer.
}

ID3D11ShaderResourceView* D3D11RenderTexture::GetShaderResourceView() const
{
	return m_shaderResourceView;
}

void D3D11RenderTexture::CreateOrthographicProjectionMatrix(float nearPlane, float farPlane)
{
	m_orthographicProjectionMatrix = Matrix::CreateOrthographicLH(float(m_width), float(m_height), nearPlane, farPlane);
}

Matrix D3D11RenderTexture::GetOrthographicProjectionMatrix() const
{
	return m_orthographicProjectionMatrix;
}
