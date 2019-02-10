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

//= INCLUDES ==================
#include "RHI_Definition.h"
#include "../Core/EngineDefs.h"
#include <memory>
#include <string>
//=============================

namespace Directus
{
	namespace Math { class Vector4; }

	class ENGINE_CLASS RHI_Device
	{
	public:
		RHI_Device(void* drawHandle);
		~RHI_Device();

		//= DRAW/PRESENT ==============================================================================
		bool Draw(unsigned int vertexCount);
		bool DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset);
		bool Present();
		//=============================================================================================

		//= CLEAR ============================================================================================
		bool ClearBackBuffer(const Math::Vector4& color);
		bool ClearRenderTarget(void* renderTarget, const Math::Vector4& color);
		bool ClearDepthStencil(void* depthStencil, unsigned int flags, float depth, unsigned int stencil = 0);
		//====================================================================================================

		//= SET ====================================================================================================
		bool SetVertexShader(void* buffer);
		bool SetPixelShader(void* buffer);
		bool SetConstantBuffers(unsigned int startSlot, unsigned int bufferCount, Buffer_Scope scope, void* buffer);
		bool SetSamplers(unsigned int startSlot, unsigned int samplerCount, void* samplers);
		bool SetTextures(unsigned int startSlot, unsigned int resourceCount, void* shaderResources);
		bool SetBackBufferAsRenderTarget();
		bool SetRenderTargets(unsigned int renderTargetCount, void* renderTargets, void* depthStencil);
		bool SetResolution(unsigned int width, unsigned int height);
		bool SetViewport(const RHI_Viewport& viewport);
		bool SetClippingRectangle(int left, int top, int right, int bottom);
		bool SetDepthStencilState(const std::shared_ptr<RHI_DepthStencilState>& depthStencilState);
		bool SetBlendState(const std::shared_ptr<RHI_BlendState>& blendState);
		bool SetPrimitiveTopology(PrimitiveTopology_Mode primitiveTopology);
		bool SetInputLayout(void* inputLayout);
		bool SetRasterizerState(const std::shared_ptr<RHI_RasterizerState>& rasterizerState);
		//==========================================================================================================

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

		//= MISC ==============================================
		bool IsInitialized()	{ return m_initialized; }
		template <typename T>
		T* GetDevice()			{ return (T*)m_device; }
		template <typename T>
		T* GetDeviceContext()	{ return (T*)m_deviceContext; }
		//=====================================================

	private:
		Texture_Format m_backBufferFormat;
		bool m_initialized		= false;
		void* m_device			= nullptr;
		void* m_deviceContext	= nullptr;
	};
}