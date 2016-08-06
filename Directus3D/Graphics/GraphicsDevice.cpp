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

//= INCLUDES ==============
#include "GraphicsDevice.h"
//=========================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

GraphicsDevice::GraphicsDevice()
{
	m_D3D11Device = nullptr;
	m_cullMode = CullBack;
	m_inputLayout = PositionTextureNormalTangent;
}

GraphicsDevice::~GraphicsDevice()
{

}

void GraphicsDevice::Initialize(HWND drawPaneHandle)
{
	m_D3D11Device = new D3D11Device();
	m_D3D11Device->Initialize(drawPaneHandle);
}

ID3D11Device* GraphicsDevice::GetDevice()
{
	return m_D3D11Device->GetDevice();
}

ID3D11DeviceContext* GraphicsDevice::GetDeviceContext()
{
	return m_D3D11Device->GetDeviceContext();
}

void GraphicsDevice::Clear(Vector4 color)
{
	m_D3D11Device->Clear(color);
}

void GraphicsDevice::Present()
{
	m_D3D11Device->Present();
}

void GraphicsDevice::ResetRenderTarget()
{
	m_D3D11Device->SetBackBufferRenderTarget();
}

void GraphicsDevice::ResetViewport()
{
	m_D3D11Device->ResetViewport();
}

void GraphicsDevice::EnableZBuffer(bool enable)
{
	enable ? m_D3D11Device->TurnZBufferOn() : m_D3D11Device->TurnZBufferOff();
}

void GraphicsDevice::EnableAlphaBlending(bool enable)
{
	enable ? m_D3D11Device->TurnOnAlphaBlending() : m_D3D11Device->TurnOffAlphaBlending();
}

bool GraphicsDevice::SetInputLayout(InputLayout inputLayout)
{
	if (m_inputLayout == inputLayout)
		return false;

	m_inputLayout = inputLayout;
	return true;
}

void GraphicsDevice::SetCullMode(CullMode cullMode)
{
	// Set face CullMode only if not already set
	if (m_cullMode == cullMode)
		return;

	// Set CullMode mode
	if (cullMode == CullBack)
		m_D3D11Device->SetFaceCullMode(D3D11_CULL_BACK);
	else if (cullMode == CullFront)
		m_D3D11Device->SetFaceCullMode(D3D11_CULL_FRONT);
	else if (cullMode == CullNone)
		m_D3D11Device->SetFaceCullMode(D3D11_CULL_NONE);

	// Save the current CullMode mode
	m_cullMode = cullMode;
}

void GraphicsDevice::SetViewport(int width, int height)
{
	m_D3D11Device->SetResolution(width, height);
}
