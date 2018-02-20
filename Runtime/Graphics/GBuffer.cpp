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

//= INCLUDES ========================
#include "GBuffer.h"
#include "D3D11/D3D11RenderTexture.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	GBuffer::GBuffer(Graphics* graphics, int width, int height)
	{
		m_graphics = graphics;

		// ALBEDO
		m_renderTargets.push_back(GBuffer_Texture{ make_unique<D3D11RenderTexture>(m_graphics, width, height, false, DXGI_FORMAT_R32G32B32A32_FLOAT),	GBuffer_Target_Albedo });
		// NORMAL
		m_renderTargets.push_back(GBuffer_Texture{ make_unique<D3D11RenderTexture>(m_graphics, width, height, false, DXGI_FORMAT_R32G32B32A32_FLOAT),	GBuffer_Target_Normal });
		// SPECULAR
		m_renderTargets.push_back(GBuffer_Texture{ make_unique<D3D11RenderTexture>(m_graphics, width, height, false, DXGI_FORMAT_R32G32B32A32_FLOAT),	GBuffer_Target_Specular });
		// DEPTH
		m_renderTargets.push_back(GBuffer_Texture{ make_unique<D3D11RenderTexture>(m_graphics, width, height, true,	DXGI_FORMAT_R32G32B32A32_FLOAT),	GBuffer_Target_Depth });	
	}

	GBuffer::~GBuffer()
	{
		m_renderTargets.clear();
	}

	bool GBuffer::SetAsRenderTarget()
	{
		if (!m_graphics->GetDeviceContext())
			return false;

		// Bind the render target view array and depth stencil buffer to the output render pipeline.
		ID3D11RenderTargetView* views[4]
		{
			m_renderTargets[0].texture->GetRenderTargetView(),
			m_renderTargets[1].texture->GetRenderTargetView(),
			m_renderTargets[2].texture->GetRenderTargetView(),
			m_renderTargets[3].texture->GetRenderTargetView()
		};
		ID3D11RenderTargetView** renderTargetViews = views;

		// Depth
		m_graphics->GetDeviceContext()->OMSetRenderTargets(unsigned int(m_renderTargets.size()), &renderTargetViews[0], m_renderTargets[3].texture->GetDepthStencilView());

		// Set the viewport.
		m_graphics->GetDeviceContext()->RSSetViewports(1, &m_renderTargets[0].texture->GetViewport());

		return true;
	}

	bool GBuffer::Clear()
	{
		if (!m_graphics->GetDeviceContext())
			return false;

		// Clear the render target buffers.
		for (auto& renderTarget : m_renderTargets)
		{
			if (!renderTarget.texture->GetDepthEnabled())
			{
				// Color buffer
				m_graphics->GetDeviceContext()->ClearRenderTargetView(renderTarget.texture->GetRenderTargetView(), Vector4::Zero.Data());
			}
			else
			{
				// Clear the depth buffer.
				m_graphics->GetDeviceContext()->ClearDepthStencilView(renderTarget.texture->GetDepthStencilView(), D3D11_CLEAR_DEPTH, renderTarget.texture->GetMaxDepth(), 0);
			}
		}
		return true;
	}

	void* GBuffer::GetShaderResource(GBuffer_Texture_Type type)
	{
		for (auto& renderTarget : m_renderTargets)
		{
			if (renderTarget.type == type)
			{
				return (void*)renderTarget.texture->GetShaderResourceView();
			}
		}

		return nullptr;
	}
}