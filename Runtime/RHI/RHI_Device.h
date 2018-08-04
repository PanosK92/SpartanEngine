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
#include "IRHI_Definition.h"
#include "RHI_Viewport.h"
#include "../Math/Vector4.h"
#include "../Core/SubSystem.h"
#include "../Profiling/Profiler.h"
//================================

namespace Directus
{
	class ENGINE_CLASS RHI_Device
	{
	public:
		RHI_Device(void* drawHandle);
		~RHI_Device();

		//= DRAW ======================================================================================
		void Draw(unsigned int vertexCount);
		void DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset);
		void Clear(const Math::Vector4& color);
		void Present();
		//=============================================================================================

		//= BIND ============================================================================================================
		void Bind_BackBufferAsRenderTarget();
		void Bind_VertexShader(void* buffer);
		void Bind_PixelShader(void* buffer);
		void Bind_ConstantBuffers(unsigned int startSlot, unsigned int bufferCount, Buffer_Scope scope, void* const* buffer);
		void Bind_Samplers(unsigned int startSlot, unsigned int samplerCount, void* const* samplers);
		void Bind_RenderTargets(unsigned int renderTargetCount, void* const* renderTargets, void* depthStencil);
		void Bind_Textures(unsigned int startSlot, unsigned int resourceCount, void* const* shaderResources);
		//===================================================================================================================

		//= RESOLUTION ===========================
		bool SetResolution(int width, int height);
		//========================================

		//= VIEWPORT ====================================
		RHI_Viewport GetViewport() { return m_viewport; }
		void SetViewport(const RHI_Viewport& viewport);
		//===============================================

		//= MISC ============================================================
		bool EnableDepth(bool enable);
		bool EnableAlphaBlending(bool enable);
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

		bool IsInitialized() { return m_initialized; }

		template <typename T>
		T* GetDevice()			{ return (T*)m_device; }
		template <typename T>
		T* GetDeviceContext()	{ return (T*)m_deviceContext; }

	private:
		Texture_Format m_format;
		RHI_Viewport m_viewport;
		bool m_depthEnabled;
		bool m_alphaBlendingEnabled;
		bool m_initialized;
		void* m_device;
		void* m_deviceContext;
	};
}