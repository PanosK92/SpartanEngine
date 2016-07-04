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

//= INCLUDES ===============
#include "GBuffer.h"
#include "../Core/Globals.h"

//==========================

GBuffer::GBuffer(GraphicsDevice* graphicsDevice)
{
	m_graphicsDevice = graphicsDevice;

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		m_renderTargetTextureArray[i] = nullptr;
		m_renderTargetViewArray[i] = nullptr;
		m_shaderResourceViewArray[i] = nullptr;
	}

	m_depthStencilBuffer = nullptr;
	m_depthStencilView = nullptr;
}

GBuffer::~GBuffer()
{
	DirectusSafeRelease(m_depthStencilView);
	DirectusSafeRelease(m_depthStencilBuffer);

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		DirectusSafeRelease(m_shaderResourceViewArray[i]);
		DirectusSafeRelease(m_renderTargetViewArray[i]);
		DirectusSafeRelease(m_renderTargetTextureArray[i]);
	}
}

bool GBuffer::Initialize(int width, int height)
{
	D3D11_TEXTURE2D_DESC textureDesc;
	HRESULT result;
	D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
	D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
	D3D11_TEXTURE2D_DESC depthBufferDesc;
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;

	// Store the width and height of the render texture.
	m_textureWidth = width;
	m_textureHeight = height;

	// Initialize the render target texture description.
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
	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		result = m_graphicsDevice->GetDevice()->CreateTexture2D(&textureDesc, nullptr, &m_renderTargetTextureArray[i]);
		if (FAILED(result))
		{
			return false;
		}
	}

	// Setup the description of the render target view.
	renderTargetViewDesc.Format = textureDesc.Format;
	renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	renderTargetViewDesc.Texture2D.MipSlice = 0;

	// Create the render target views.
	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		result = m_graphicsDevice->GetDevice()->CreateRenderTargetView(m_renderTargetTextureArray[i], &renderTargetViewDesc, &m_renderTargetViewArray[i]);
		if (FAILED(result))
			return false;
	}

	// Setup the description of the shader resource view.
	shaderResourceViewDesc.Format = textureDesc.Format;
	shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	shaderResourceViewDesc.Texture2D.MipLevels = 1;

	// Create the shader resource views.
	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		result = m_graphicsDevice->GetDevice()->CreateShaderResourceView(m_renderTargetTextureArray[i], &shaderResourceViewDesc, &m_shaderResourceViewArray[i]);
		if (FAILED(result))
			return false;
	}

	// Initialize the description of the depth buffer.
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
	result = m_graphicsDevice->GetDevice()->CreateTexture2D(&depthBufferDesc, nullptr, &m_depthStencilBuffer);
	if (FAILED(result))
	{
		return false;
	}

	// Initailze the depth stencil view description.
	ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	// Create the depth stencil view.
	result = m_graphicsDevice->GetDevice()->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView);
	if (FAILED(result))
	{
		return false;
	}

	// Setup the viewport for rendering.
	m_viewport.Width = static_cast<float>(width);
	m_viewport.Height = static_cast<float>(height);
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = 1.0f;
	m_viewport.TopLeftX = 0.0f;
	m_viewport.TopLeftY = 0.0f;

	return true;
}

void GBuffer::SetRenderTargets()
{
	// Bind the render target view array and depth stencil buffer to the output render pipeline.
	m_graphicsDevice->GetDeviceContext()->OMSetRenderTargets(BUFFER_COUNT, m_renderTargetViewArray, m_depthStencilView);

	// Set the viewport.
	m_graphicsDevice->GetDeviceContext()->RSSetViewports(1, &m_viewport);
}

void GBuffer::ClearRenderTargets(float red, float green, float blue, float alpha)
{
	float color[4];

	// Setup the color to clear the buffer to.
	color[0] = red;
	color[1] = green;
	color[2] = blue;
	color[3] = alpha;

	// Clear the render target buffers.
	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		m_graphicsDevice->GetDeviceContext()->ClearRenderTargetView(m_renderTargetViewArray[i], color);
	}

	// Clear the depth buffer.
	m_graphicsDevice->GetDeviceContext()->ClearDepthStencilView(m_depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

ID3D11ShaderResourceView* GBuffer::GetShaderResourceView(int index)
{
	return m_shaderResourceViewArray[index];
}
