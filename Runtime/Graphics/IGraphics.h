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

//= INCLUDES ====================
#include "../Math/Vector4.h"
#include "../Core/SubSystem.h"
#include "../Core/Backends_Def.h"
#include "Viewport.h"
//===============================

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

	enum FillMode
	{
		FillMode_Solid,
		FillMode_Wireframe
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

	enum Texture_Format
	{
		Texture_Format_R32G32B32A32_FLOAT,
		Texture_Format_R16G16B16A16_FLOAT,
		Texture_Format_R8G8B8A8_UNORM,
		Texture_Format_R8_UNORM
	};

	class IGraphics : public Subsystem
	{
	public:
		IGraphics(Context* context) : Subsystem(context)
		{
			m_primitiveTopology		= TriangleList;
			m_inputLayout			= PositionTextureTBN;
			m_cullMode				= CullBack;
			m_backBuffer_format		= Texture_Format_R8G8B8A8_UNORM;
			m_depthEnabled			= true;
			m_maxDepth				= 1.0f;
			m_alphaBlendingEnabled	= false;
			m_drawHandle			= nullptr;
		}
		virtual ~IGraphics() {}

		//= BINDING ===========================================================
		virtual void SetHandle(void* drawHandle) { m_drawHandle = drawHandle; }
		//=====================================================================

		//=================================================
		virtual void Clear(const Math::Vector4& color) = 0;
		virtual void Present() = 0;
		virtual void SetBackBufferAsRenderTarget() = 0;
		//=================================================

		//= DEPTH ==============================================================================================
		virtual bool CreateDepthStencilState(void* depthStencilState, bool depthEnabled, bool writeEnabled) = 0;
		virtual bool CreateDepthStencilBuffer() = 0;
		virtual bool CreateDepthStencilView() = 0;
		virtual bool EnableDepth(bool enable)
		{
			if (m_depthEnabled == enable)
				return false;

			m_depthEnabled = enable;
			return true;
		}
		//======================================================================================================

		//= ALPHA BLENDING ==========================
		virtual bool EnableAlphaBlending(bool enable)
		{
			if (m_alphaBlendingEnabled == enable)
				return false;

			m_alphaBlendingEnabled = enable;
			return true;
		}
		//===========================================

		// CULL MODE ==============================
		virtual CullMode GetCullMode() = 0;
		virtual bool SetCullMode(CullMode cullMode)
		{
			if (m_cullMode == cullMode)
				return false;

			m_cullMode = cullMode;
			return true;
		}
		//=========================================

		//= PRIMITIVE TOPOLOGY =====================
		bool SetInputLayout(InputLayout inputLayout)
		{
			if (m_inputLayout == inputLayout)
				return false;

			m_inputLayout = inputLayout;
			return true;
		}
		//==========================================

		//= INPUT LAYOUT =====================================================
		virtual bool SetPrimitiveTopology(PrimitiveTopology primitiveTopology)
		{
			if (m_primitiveTopology == primitiveTopology)
				return false;

			m_primitiveTopology = primitiveTopology;
			return true;
		}
		//====================================================================

		//= VIEWPORT ===========================================
		virtual bool SetResolution(int width, int height) = 0;
		virtual const Viewport& GetViewport() = 0;
		virtual void SetViewport(float width, float height) = 0;
		virtual void SetViewport() = 0;
		virtual float GetMaxDepth() = 0;
		//======================================================

		//= EVENTS =======================================
		virtual void EventBegin(const std::string& name){}
		virtual void EventEnd(){}
		//================================================

		virtual bool IsInitialized() = 0;

	protected:
		PrimitiveTopology m_primitiveTopology;
		InputLayout m_inputLayout;
		CullMode m_cullMode;
		Texture_Format m_backBuffer_format;
		Viewport m_backBuffer_viewport;
		bool m_depthEnabled;
		bool m_alphaBlendingEnabled;
		void* m_drawHandle;
		float m_maxDepth;
	};
}