/*
Copyright(c) 2016-2017 Panos Karabelas

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
#include "../Math/Vector4.h"
#include "../Core/Subsystem.h"
#include "GraphicsDefinitions.h"
//==============================

namespace Directus
{
	class IGraphicsDevice : public Subsystem
	{
	public:
		IGraphicsDevice(Context* context) : Subsystem(context) {}
		virtual ~IGraphicsDevice() {}

		//==========================================================================
		virtual void SetHandle(void* drawHandle) = 0;
		virtual void Clear(const Math::Vector4& color) = 0;
		virtual void Present() = 0;
		virtual void SetBackBufferAsRenderTarget() = 0;
		//==========================================================================

		//= DEPTH ==============================================================================================
		virtual bool CreateDepthStencilState(void* depthStencilState, bool depthEnabled, bool writeEnabled) = 0;
		virtual bool CreateDepthStencilBuffer() = 0;
		virtual bool CreateDepthStencilView() = 0;
		virtual void EnableDepth(bool enable) = 0;
		//======================================================================================================

		//=========================================================================
		virtual void EnableAlphaBlending(bool enable) = 0;
		virtual void SetInputLayout(InputLayout inputLayout) = 0;
		virtual CullMode GetCullMode() = 0;
		virtual void SetCullMode(CullMode cullMode) = 0;
		virtual void SetPrimitiveTopology(PrimitiveTopology primitiveTopology) = 0;
		//=========================================================================

		//= VIEWPORT ==============================================================
		virtual bool SetResolution(int width, int height) = 0;
		virtual void* GetViewport() = 0;
		virtual void SetViewport(float width, float height) = 0;
		virtual void ResetViewport() = 0;
		virtual float GetMaxDepth() = 0;
		//=========================================================================

		virtual bool IsInitialized() = 0;

	protected:
		InputLayout m_inputLayout;
		CullMode m_cullMode;
		PrimitiveTopology m_primitiveTopology;
		bool m_depthEnabled;
		bool m_alphaBlendingEnabled;
	};
}