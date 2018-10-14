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

//= INCLUDES ==============
#include "RHI_Definition.h"
#include "RHI_Viewport.h"
#include "RHI_Object.h"
#include "..\Math\Matrix.h"
//=========================

namespace Directus
{
	class ENGINE_CLASS RHI_RenderTexture : public RHI_Object
	{
	public:
		RHI_RenderTexture(
			std::shared_ptr<RHI_Device> rhiDevice,
			int width						= Settings::Get().GetResolutionWidth(),
			int height						= Settings::Get().GetResolutionHeight(),
			Texture_Format textureFormat	= Texture_Format_R8G8B8A8_UNORM,
			bool depth						= false,
			Texture_Format depthFormat		= Texture_Format_D32_FLOAT
		);
		~RHI_RenderTexture();

		bool Clear(const Math::Vector4& clearColor);
		bool Clear(float red, float green, float blue, float alpha);
		void ComputeOrthographicProjectionMatrix(float nearPlane, float farPlane);
		void* GetRenderTargetView();
		void* GetShaderResource();
		void* GetDepthStencilView();
		const Math::Matrix& GetOrthographicProjectionMatrix()	{ return m_orthographicProjectionMatrix; }
		const RHI_Viewport& GetViewport()						{ return m_viewport; }
		bool GetDepthEnabled()									{ return m_depthEnabled; }
		unsigned int GetWidth()									{ return m_width; }
		unsigned int GetHeight()								{ return m_height; }

	protected:
		bool m_depthEnabled = false;
		float m_nearPlane, m_farPlane;
		Math::Matrix m_orthographicProjectionMatrix;
		RHI_Viewport m_viewport;
		Texture_Format m_format;
		std::shared_ptr<RHI_Device> m_rhiDevice;
		unsigned int m_width;
		unsigned int m_height;

		// D3D11
		void* m_renderTargetTexture;
		void* m_renderTargetView;
		void* m_shaderResourceView;	
		void* m_depthStencilBuffer;
		void* m_depthStencilView;
	};
}