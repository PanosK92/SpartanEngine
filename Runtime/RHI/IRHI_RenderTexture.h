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
#include "../../Core/Settings.h"
#include "IRHI_Definition.h"
#include "RHI_Viewport.h"
#include <vector>
//==============================

namespace Directus
{
	class ENGINE_CLASS IRHI_RenderTexture
	{
	public:
		IRHI_RenderTexture(
			std::shared_ptr<RHI_Device> rhiDevice,
			int width				= Settings::Get().GetResolutionWidth(),
			int height				= Settings::Get().GetResolutionHeight(),
			bool depth				= false,
			Texture_Format format	= Texture_Format_R32G32B32A32_FLOAT
		){}
		~IRHI_RenderTexture() {};


		virtual bool SetAsRenderTarget() = 0;
		virtual bool Clear(const Math::Vector4& clearColor) = 0;
		virtual bool Clear(float red, float green, float blue, float alpha) = 0;
		virtual void ComputeOrthographicProjectionMatrix(float nearPlane, float farPlane) = 0;	
		virtual void* GetTexture() = 0;
		virtual void* GetRenderTargetView() = 0;
		virtual void* GetShaderResourceView() = 0;
		virtual void* GetDepthStencilView() = 0;

		const Math::Matrix& GetOrthographicProjectionMatrix()	{ return m_orthographicProjectionMatrix; }
		const RHI_Viewport& GetViewport()						{ return m_viewport; }
		bool GetDepthEnabled()									{ return m_depthEnabled; }

	protected:
		bool m_depthEnabled;

		// Projection matrix
		float m_nearPlane, m_farPlane;
		Math::Matrix m_orthographicProjectionMatrix;

		RHI_Viewport m_viewport;
	};
}