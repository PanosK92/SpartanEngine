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

//= INCLUDES ========
#include "Graphics.h"
#if defined(D3D12)
#include "D3D12/D3D12Graphics.h"
#elif defined(D3D11)
#include "D3D11/D3D11Graphics.h"
#endif
//===================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

Graphics::Graphics(Context* context): Subsystem(context) 
{
	m_graphicsAPI = nullptr;
	m_inputLayout = PositionTextureNormalTangent;
	m_cullMode = CullBack;
	m_primitiveTopology = TriangleList;
	m_zBufferEnabled = true;
	m_alphaBlendingEnabled = false;
}

Graphics::~Graphics()
{

}

void Graphics::Initialize(HWND drawPaneHandle)
{
	m_graphicsAPI = make_shared<D3D11Graphics>();
	m_graphicsAPI->Initialize(drawPaneHandle);
}

Device* Graphics::GetDevice()
{
	return m_graphicsAPI->GetDevice();
}

DeviceContext* Graphics::GetDeviceContext()
{
	return m_graphicsAPI->GetDeviceContext();
}

void Graphics::Clear(const Vector4& color)
{
	m_graphicsAPI->Clear(color);
}

void Graphics::Present()
{
	m_graphicsAPI->Present();
}

void Graphics::ResetRenderTarget()
{
	m_graphicsAPI->SetBackBufferRenderTarget();
}

void Graphics::ResetViewport()
{
	m_graphicsAPI->ResetViewport();
}

void Graphics::EnableZBuffer(bool enable)
{
	if (m_zBufferEnabled == enable)
		return;

	m_graphicsAPI->EnableZBuffer(enable);
	m_zBufferEnabled = enable;
}

void Graphics::EnableAlphaBlending(bool enable)
{
	if (m_alphaBlendingEnabled == enable)
		return;

	m_graphicsAPI->EnabledAlphaBlending(enable);
	m_alphaBlendingEnabled = enable;
}

void Graphics::SetInputLayout(InputLayout inputLayout)
{
	if (m_inputLayout == inputLayout)
		return;

	m_inputLayout = inputLayout;
}

void Graphics::SetCullMode(CullMode cullMode)
{
	// Set face CullMode only if not already set
	if (m_cullMode == cullMode)
		return;

	// Set CullMode mode
	if (cullMode == CullBack)
		m_graphicsAPI->SetFaceCullMode(D3D11_CULL_BACK);
	else if (cullMode == CullFront)
		m_graphicsAPI->SetFaceCullMode(D3D11_CULL_FRONT);
	else if (cullMode == CullNone)
		m_graphicsAPI->SetFaceCullMode(D3D11_CULL_NONE);

	// Save the current CullMode mode
	m_cullMode = cullMode;
}

void Graphics::SetPrimitiveTopology(PrimitiveTopology primitiveTopology)
{
	// Set PrimitiveTopology only if not already set
	if (m_primitiveTopology == primitiveTopology)
		return;

	// Set PrimitiveTopology
	if (primitiveTopology == TriangleList)
		m_graphicsAPI->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	else if (primitiveTopology == LineList)
		m_graphicsAPI->GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	// Save the current PrimitiveTopology mode
	m_primitiveTopology = primitiveTopology;
}

void Graphics::SetViewport(int width, int height)
{
	m_graphicsAPI->SetResolution(width, height);
}
