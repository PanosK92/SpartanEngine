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

//= INCLUDES =====================
#include "../IRHI_RenderTexture.h"
//================================

namespace Directus
{
	class ENGINE_CLASS D3D11_RenderTexture : public IRHI_RenderTexture
	{
	public:
		D3D11_RenderTexture(
			RHI_Device* rhiDevice, 
			int width				= Settings::Get().GetResolutionWidth(), 
			int height				= Settings::Get().GetResolutionHeight(), 
			bool depth				= false,
			Texture_Format format	= Texture_Format_R32G32B32A32_FLOAT
		);
		~D3D11_RenderTexture();

		bool SetAsRenderTarget() override;
		bool Clear(const Math::Vector4& clearColor) override;
		bool Clear(float red, float green, float blue, float alpha) override;
		void ComputeOrthographicProjectionMatrix(float nearPlane, float farPlane) override;
		void* GetTexture() override				{ return m_renderTargetTexture; }
		void* GetRenderTargetView() override	{ return m_renderTargetView; }
		void* GetShaderResourceView() override	{ return m_shaderResourceView; }
		void* GetDepthStencilView() override	{ return m_depthStencilView; }

	private:
		bool Construct();

		// Texture
		ID3D11Texture2D* m_renderTargetTexture;
		ID3D11RenderTargetView* m_renderTargetView;
		ID3D11ShaderResourceView* m_shaderResourceView;
		Texture_Format m_format;

		// Depth texture	
		ID3D11Texture2D* m_depthStencilBuffer;
		ID3D11DepthStencilView* m_depthStencilView;	
	
		RHI_Device* m_rhiDevice;
	};
}