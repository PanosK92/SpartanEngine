/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ================
#include "RHI_Definition.h"
#include "RHI_Viewport.h"
#include "RHI_Object.h"
#include "..\Math\Matrix.h"
#include "..\Core\Settings.h"
//===========================

namespace Directus
{
	class ENGINE_CLASS RHI_RenderTexture : public RHI_Object
	{
	public:
		RHI_RenderTexture(
			std::shared_ptr<RHI_Device> rhiDevice,
			unsigned int width,
			unsigned int height,
			RHI_Format textureFormat	= Format_R8G8B8A8_UNORM,
			bool depth						= false,
			RHI_Format depthFormat		= Format_D32_FLOAT,
			unsigned int arraySize			= 1
		);
		~RHI_RenderTexture();

		bool Clear(const Math::Vector4& clearColor);
		bool Clear(float red, float green, float blue, float alpha);
		void* GetRenderTargetView(unsigned int index = 0)	{ return index < m_renderTargetViews.size() ? m_renderTargetViews[index] : nullptr; }
		void* GetShaderResource()							{ return m_shaderResourceView; }
		void* GetDepthStencilView()							{ return m_depthStencilView; }
		const RHI_Viewport& GetViewport()					{ return m_viewport; }
		bool GetDepthEnabled()								{ return m_depthEnabled; }
		unsigned int GetWidth()								{ return m_width; }
		unsigned int GetHeight()							{ return m_height; }
		unsigned int GetArraySize()							{ return m_arraySize; }
		RHI_Format GetFormat()							{ return m_format; }

	protected:
		bool m_depthEnabled	= false;
		float m_nearPlane	= 0;
		float m_farPlane	= 0;
		RHI_Viewport m_viewport;
		RHI_Format m_format;
		std::shared_ptr<RHI_Device> m_rhiDevice;
		unsigned int m_width;
		unsigned int m_height;
		unsigned int m_arraySize;

		// D3D11
		std::vector<void*> m_renderTargetViews;
		void* m_renderTargetTexture = nullptr;
		void* m_shaderResourceView	= nullptr;
		void* m_depthStencilTexture	= nullptr;
		void* m_depthStencilView	= nullptr;
	};
}