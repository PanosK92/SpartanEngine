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

//= INCLUDES ======================
#include "../Math/Vector4.h"
#include "../Core/SubSystem.h"
#include "../Core/EngineBackends.h"
//=================================

namespace Directus
{
	enum PrimitiveTopology
	{
		TriangleList,
		LineList
	};

	enum InputLayout
	{
		Auto,
		Position,
		PositionColor,
		PositionTexture,
		PositionTextureTBN
	};

	enum CullMode
	{
		CullNone,
		CullFront,
		CullBack
	};

	enum Texture_Sampler_Filter
	{
		Texture_Sampler_Point,
		Texture_Sampler_Bilinear,
		Texture_Sampler_Linear,	
		Texture_Sampler_Anisotropic
	};

	enum Texture_Address_Mode
	{
		Texture_Address_Wrap,
        Texture_Address_Mirror,
        Texture_Address_Clamp,
        Texture_Address_Border,
        Texture_Address_MirrorOnce,
	};

	enum Texture_Comparison_Function
	{
		Texture_Comparison_Never,
        Texture_Comparison_Less,
        Texture_Comparison_Equal,
        Texture_Comparison_LessEqual,
        Texture_Comparison_Greater,
        Texture_Comparison_NotEqual,
        Texture_Comparison_GreaterEqual,
        Texture_Comparison_Always
	};

	class IGraphics : public Subsystem
	{
	public:
		IGraphics(Context* context) : Subsystem(context)
		{
			m_primitiveTopology		= TriangleList;
			m_inputLayout			= PositionTextureTBN;
			m_cullMode				= CullBack;
			m_depthEnabled			= true;
			m_alphaBlendingEnabled	= true;
		}
		virtual ~IGraphics() {}

		//=================================================
		virtual void SetHandle(void* drawHandle) = 0;
		virtual void Clear(const Math::Vector4& color) = 0;
		virtual void Present() = 0;
		virtual void SetBackBufferAsRenderTarget() = 0;
		//=================================================

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

		//= VIEWPORT ===========================================
		virtual bool SetResolution(int width, int height) = 0;
		virtual void* GetViewport() = 0;
		virtual void SetViewport(float width, float height) = 0;
		virtual void SetViewport() = 0;
		virtual float GetMaxDepth() = 0;
		//======================================================

		virtual bool IsInitialized() = 0;

	protected:
		PrimitiveTopology m_primitiveTopology;
		InputLayout m_inputLayout;
		CullMode m_cullMode;	
		bool m_depthEnabled;
		bool m_alphaBlendingEnabled;
	};
}