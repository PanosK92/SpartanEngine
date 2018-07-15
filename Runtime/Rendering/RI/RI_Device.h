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

//= INCLUDES ========================
#include "../../Math/Vector4.h"
#include "../../Core/SubSystem.h"
#include "Backend_Def.h"
#include "RI_Viewport.h"
#include "../../Profiling/Profiler.h"
//===================================

namespace Directus
{
	#define RESOURCES_FROM_VECTOR(vector) 0, (unsigned int)vector.size(), &vector[0]

	class RI_Device : public Subsystem
	{
	public:
		RI_Device(Context* context) : Subsystem(context)
		{
			m_primitiveTopology		= PrimitiveTopology_NotAssigned;
			m_inputLayout			= Input_NotAssigned;
			m_cullMode				= Cull_NotAssigned;
			m_format				= Texture_Format_R8G8B8A8_UNORM;
			m_depthEnabled			= true;
			m_alphaBlendingEnabled	= false;
			m_drawHandle			= nullptr;
			m_maxDepth				= 1.0f;
		}
		virtual ~RI_Device() {}

		//= RENDERING ============================================================================================================================
		virtual void Draw(unsigned int vertexCount)																{ Profiler::Get().m_drawCalls++; }
		virtual void DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset)	{ Profiler::Get().m_drawCalls++; }
		virtual void Clear(const Math::Vector4& color) = 0;
		virtual void Present(){}
		//========================================================================================================================================

		//= BINDING ========================================================================================================
		virtual void SetHandle(void* drawHandle) { m_drawHandle = drawHandle; }
		virtual void Bind_BackBufferAsRenderTarget() = 0;
		virtual void Bind_RenderTargets(unsigned int renderTargetCount, void* const* renderTargets, void* depthStencil) = 0;
		virtual void Bind_Textures(unsigned int startSlot, unsigned int resourceCount, void* const* shaderResources) = 0;
		void Bind_Texture(unsigned int startSlot, void* shaderResource) { Bind_Textures(startSlot, 1, &shaderResource); }
		virtual void Bind_Samplers(unsigned int startSlot, unsigned int samplerCount, void* const* samplers) = 0;
		void Bind_Sampler(unsigned int startSlot, void* sampler) { Bind_Samplers(startSlot, 1, &sampler); }
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
		virtual const RI_Viewport& GetViewport() = 0;
		virtual void SetViewport(const RI_Viewport& viewport) = 0;
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
		const RI_Viewport& GetBackBufferViewport() { return m_backBufferViewport; }
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

		// CULL MODE ==============================
		virtual Cull_Mode GetCullMode() = 0;
		virtual bool SetCullMode(Cull_Mode cullMode)
		{
			if (m_cullMode == cullMode)
				return false;

			m_cullMode = cullMode;
			return true;
		}
		//=========================================

		//= PRIMITIVE TOPOLOGY =====================================================
		virtual bool Set_PrimitiveTopology(PrimitiveTopology_Mode primitiveTopology)
		{
			if (m_primitiveTopology == primitiveTopology)
				return false;

			m_primitiveTopology = primitiveTopology;
			return true;
		}
		//==========================================================================

		//= INPUT LAYOUT =============================
		bool Set_InputLayout(Input_Layout inputLayout)
		{
			if (m_inputLayout == inputLayout)
				return false;

			m_inputLayout = inputLayout;
			return true;
		}
		//============================================

		//= PROFILING ====================================
		virtual void EventBegin(const std::string& name){}
		virtual void EventEnd(){}
		virtual void QueryBegin(){}
		virtual void QueryEnd(){}
		//================================================

		virtual bool IsInitialized() = 0;

	protected:
		PrimitiveTopology_Mode m_primitiveTopology;
		Input_Layout m_inputLayout;
		Cull_Mode m_cullMode;
		Texture_Format m_format;
		RI_Viewport m_backBufferViewport;
		bool m_depthEnabled;
		bool m_alphaBlendingEnabled;
		void* m_drawHandle;
		float m_maxDepth;
	};
}