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
#include "RHI_Definition.h"
#include "RHI_Viewport.h"
#include "RHI_PipelineState.h"
#include "../Math/Vector4.h"
#include "../Core/SubSystem.h"
#include "../Profiling/Profiler.h"
//================================

namespace Directus
{
	class ENGINE_CLASS IRHI_Device : public Subsystem
	{
	public:
		IRHI_Device(Context* context) : Subsystem(context)
		{
			m_pipelineState			= std::make_shared<RHI_PipelineState>((RHI_Device*)this);
			m_format				= Texture_Format_R8G8B8A8_UNORM;
			m_depthEnabled			= true;
			m_alphaBlendingEnabled	= false;
			m_drawHandle			= nullptr;
			m_maxDepth				= 1.0f;
		}
		virtual ~IRHI_Device() {}

		//= RENDERING ============================================================================================================================
		virtual void Draw(unsigned int vertexCount)																{ Profiler::Get().m_drawCalls++; }
		virtual void DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset)	{ Profiler::Get().m_drawCalls++; }
		virtual void Clear(const Math::Vector4& color) = 0;
		virtual void Present() = 0;
		//========================================================================================================================================

		//= BINDING ========================================================================================================
		virtual void SetHandle(void* drawHandle) { m_drawHandle = drawHandle; }
		virtual void Bind_BackBufferAsRenderTarget() = 0;
		virtual void Bind_RenderTargets(unsigned int renderTargetCount, void* const* renderTargets, void* depthStencil) = 0;
		virtual void Bind_Textures(unsigned int startSlot, unsigned int resourceCount, void* const* shaderResources) = 0;
		//==================================================================================================================

		//= RESOLUTION ==================================
		virtual bool SetResolution(int width, int height)
		{
			if (width == 0 || height == 0)
				return false;

			return true;
		}
		//===============================================

		//= VIEWPORT =============================================================
		virtual const RHI_Viewport& GetViewport() { return RHI_Viewport(); }
		virtual void SetViewport(const RHI_Viewport& viewport){}
		float GetMaxDepth() { return m_maxDepth; }

		void SetBackBufferViewport(float width = 0, float height = 0) 
		{ 
			if (width == 0 || height == 0)
			{
				m_backBufferViewport.SetWidth(width);
				m_backBufferViewport.SetHeight(height);
			}
			SetViewport(m_backBufferViewport); 
		}
		const RHI_Viewport& GetBackBufferViewport() { return m_backBufferViewport; }
		//========================================================================

		//= DEPTH ===========================
		virtual bool EnableDepth(bool enable)
		{
			if (m_depthEnabled == enable)
				return false;

			m_depthEnabled = enable;
			return true;
		}
		//===================================

		//= ALPHA BLENDING ==========================
		virtual bool EnableAlphaBlending(bool enable)
		{
			if (m_alphaBlendingEnabled == enable)
				return false;

			m_alphaBlendingEnabled = enable;
			return true;
		}
		//===========================================

		/*Cull mode */			virtual bool Set_CullMode(Cull_Mode cullMode)									{ return false; }
		/*Primitive topology*/	virtual bool Set_PrimitiveTopology(PrimitiveTopology_Mode primitiveTopology)	{ return false; }	
		/*Fill mode*/			virtual bool Set_FillMode(Fill_Mode fillMode)									{ return false; }
		/*Input layout*/		virtual bool Set_InputLayout(void* inputLayout)									{ return false; }

		//= PROFILING =======================================
		virtual void EventBegin(const std::string& name) = 0;
		virtual void EventEnd() = 0;
		virtual void QueryBegin() = 0;
		virtual void QueryEnd() = 0;
		//===================================================

		virtual bool IsInitialized() = 0;

		std::shared_ptr<RHI_PipelineState> GetPipelineState() { return m_pipelineState; }

	protected:
		std::shared_ptr<RHI_PipelineState> m_pipelineState;
		Texture_Format m_format;
		RHI_Viewport m_backBufferViewport;
		bool m_depthEnabled;
		bool m_alphaBlendingEnabled;
		void* m_drawHandle;
		float m_maxDepth;
	};
}