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
//=========================

namespace Directus
{
	class ENGINE_CLASS RHI_Device
	{
	public:
		RHI_Device(void* drawHandle);
		~RHI_Device();

		//= DRAW ========================================================================================
		void Draw(unsigned int vertexCount);
		void DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset);
		void ClearBackBuffer(const Math::Vector4& color);
		void ClearRenderTarget(void* renderTarget, const Math::Vector4& color);
		void ClearDepthStencil(void* depthStencil, unsigned int flags, float depth, uint8_t stencil = 0);
		void Present();
		//===============================================================================================

		//= BIND ============================================================================================================
		void Set_BackBufferAsRenderTarget();
		void Set_VertexShader(void* buffer);
		void Set_PixelShader(void* buffer);
		void Set_ConstantBuffers(unsigned int startSlot, unsigned int bufferCount, Buffer_Scope scope, void* const* buffer);
		void Set_Samplers(unsigned int startSlot, unsigned int samplerCount, void* const* samplers);
		void Set_RenderTargets(unsigned int renderTargetCount, void* const* renderTargets, void* depthStencil);
		void Set_Textures(unsigned int startSlot, unsigned int resourceCount, void* const* shaderResources);
		//===================================================================================================================

		//= RESOLUTION ==============================================
		bool Set_Resolution(unsigned int width, unsigned int height);
		//===========================================================

		//= VIEWPORT =====================================
		RHI_Viewport Get_Viewport() { return m_viewport; }
		void Set_Viewport(const RHI_Viewport& viewport);
		//================================================

		//= MISC ============================================================
		bool Set_DepthEnabled(bool enable);
		bool Set_AlphaBlendingEnabled(bool enable);
		bool Set_CullMode(Cull_Mode cullMode);
		bool Set_PrimitiveTopology(PrimitiveTopology_Mode primitiveTopology);
		bool Set_FillMode(Fill_Mode fillMode);
		bool Set_InputLayout(void* inputLayout);
		//===================================================================

		//= EVENTS ==============================
		void EventBegin(const std::string& name);
		void EventEnd();
		//=======================================

		//= PROFILING ====================================================================
		bool Profiling_CreateQuery(void** buffer, Query_Type type);
		void Profiling_QueryStart(void* queryObject);
		void Profiling_QueryEnd(void* queryObject);
		void Profiling_GetTimeStamp(void* queryDisjoint);
		float Profiling_GetDuration(void* queryDisjoint, void* queryStart, void* queryEnd);
		//=================================================================================

		bool IsInitialized()	{ return m_initialized; }

		template <typename T>
		T* GetDevice()			{ return (T*)m_device; }
		template <typename T>
		T* GetDeviceContext()	{ return (T*)m_deviceContext; }

	private:
		Texture_Format m_format;
		RHI_Viewport m_viewport;
		bool m_depthEnabled;
		bool m_alphaBlendingEnabled;
		bool m_initialized		= false;
		void* m_device			= nullptr;
		void* m_deviceContext	= nullptr;
	};
}