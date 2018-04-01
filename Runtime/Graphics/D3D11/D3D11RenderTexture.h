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

#pragma once

//= INCLUDES ===================
#include "../../Math/Matrix.h"
#include "D3D11GraphicsDevice.h"
#include "../../Core/Settings.h"
//==============================

namespace Directus
{
	class D3D11RenderTexture
	{
	public:
		D3D11RenderTexture(
			D3D11GraphicsDevice* graphicsDevice, 
			int width			= RESOLUTION_WIDTH, 
			int height			= RESOLUTION_HEIGHT, 
			bool depth			= false, 
			DXGI_FORMAT format	= DXGI_FORMAT_R32G32B32A32_FLOAT
		);
		~D3D11RenderTexture();

		bool SetAsRenderTarget();
		bool Clear(const Math::Vector4& clearColor);
		bool Clear(float red, float green, float blue, float alpha);
		void ComputeOrthographicProjectionMatrix(float nearPlane, float farPlane);
		const Math::Matrix& GetOrthographicProjectionMatrix() { return m_orthographicProjectionMatrix; }

		ID3D11Texture2D* GetTexture() { return m_renderTargetTexture; }
		ID3D11RenderTargetView* GetRenderTargetView() { return m_renderTargetView; }
		ID3D11ShaderResourceView* GetShaderResourceView() { return m_shaderResourceView; }
		ID3D11DepthStencilView* GetDepthStencilView() { return m_depthStencilView; }
		float GetMaxDepth() { return m_maxDepth; }
		const D3D11_VIEWPORT& GetViewport() { return m_viewport; }
		bool GetDepthEnabled() { return m_depthEnabled; }

	private:
		bool Construct();

		// Texture
		ID3D11Texture2D* m_renderTargetTexture;
		ID3D11RenderTargetView* m_renderTargetView;
		ID3D11ShaderResourceView* m_shaderResourceView;
		DXGI_FORMAT m_format;

		// Depth texture
		bool m_depthEnabled;
		float m_maxDepth;
		ID3D11Texture2D* m_depthStencilBuffer;
		ID3D11DepthStencilView* m_depthStencilView;	

		// Projection matrix
		float m_nearPlane, m_farPlane;
		Math::Matrix m_orthographicProjectionMatrix;

		// Dimensions
		D3D11_VIEWPORT m_viewport;
		int m_width;
		int m_height;

		D3D11GraphicsDevice* m_graphics;
	};
}