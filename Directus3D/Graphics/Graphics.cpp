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
#include "Graphics.h"
//=========================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

Graphics::Graphics()
{
	m_d3d11Graphics = nullptr;
	m_cullMode = CullBack;
	m_inputLayout = PositionTextureNormalTangent;
}

Graphics::~Graphics()
{

}

void Graphics::Initialize(HWND drawPaneHandle)
{
	m_d3d11Graphics = make_shared<D3D11Graphics>();
	m_d3d11Graphics->Initialize(drawPaneHandle);
}

ID3D11Device* Graphics::GetDevice()
{
	return m_d3d11Graphics->GetDevice();
}

ID3D11DeviceContext* Graphics::GetDeviceContext()
{
	return m_d3d11Graphics->GetDeviceContext();
}

void Graphics::Clear(const Vector4& color)
{
	m_d3d11Graphics->Clear(color);
}

void Graphics::Present()
{
	m_d3d11Graphics->Present();
}

void Graphics::ResetRenderTarget()
{
	m_d3d11Graphics->SetBackBufferRenderTarget();
}

void Graphics::ResetViewport()
{
	m_d3d11Graphics->ResetViewport();
}

void Graphics::EnableZBuffer(bool enable)
{
	m_d3d11Graphics->EnableZBuffer(enable);
}

void Graphics::EnableAlphaBlending(bool enable)
{
	m_d3d11Graphics->EnabledAlphaBlending(enable);
}

bool Graphics::SetInputLayout(InputLayout inputLayout)
{
	if (m_inputLayout == inputLayout)
		return false;

	m_inputLayout = inputLayout;
	return true;
}

void Graphics::SetCullMode(CullMode cullMode)
{
	// Set face CullMode only if not already set
	if (m_cullMode == cullMode)
		return;

	// Set CullMode mode
	if (cullMode == CullBack)
		m_d3d11Graphics->SetFaceCullMode(D3D11_CULL_BACK);
	else if (cullMode == CullFront)
		m_d3d11Graphics->SetFaceCullMode(D3D11_CULL_FRONT);
	else if (cullMode == CullNone)
		m_d3d11Graphics->SetFaceCullMode(D3D11_CULL_NONE);

	// Save the current CullMode mode
	m_cullMode = cullMode;
}

void Graphics::SetViewport(int width, int height)
{
	m_d3d11Graphics->SetResolution(width, height);
}
