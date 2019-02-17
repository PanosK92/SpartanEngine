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
	namespace Math
	{ 
		class Vector4;
		class Rectangle;
	}

	class ENGINE_CLASS RHI_Device
	{
	public:
		RHI_Device(void* drawHandle);
		~RHI_Device();

		//= DRAW/PRESENT ==============================================================================
		bool Draw(unsigned int vertexCount);
		bool DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset);
		//=============================================================================================

		//= CLEAR ============================================================================================
		bool ClearRenderTarget(void* renderTarget, const Math::Vector4& color);
		bool ClearDepthStencil(void* depthStencil, unsigned int flags, float depth, unsigned int stencil = 0);
		//====================================================================================================

		//= SET ========================================================================================================
		bool SetVertexBuffer(const std::shared_ptr<RHI_VertexBuffer>& buffer);
		bool SetIndexBuffer(const std::shared_ptr<RHI_IndexBuffer>& buffer);
		bool SetVertexShader(const std::shared_ptr<RHI_Shader>& shader);
		bool SetPixelShader(const std::shared_ptr<RHI_Shader>& shader);
		bool SetDepthStencilState(const std::shared_ptr<RHI_DepthStencilState>& depthStencilState);
		bool SetRasterizerState(const std::shared_ptr<RHI_RasterizerState>& rasterizerState);
		bool SetBlendState(const std::shared_ptr<RHI_BlendState>& blendState);
		bool SetInputLayout(const std::shared_ptr<RHI_InputLayout>& inputLayout);	
		bool SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitiveTopology);
		bool SetConstantBuffers(unsigned int startSlot, unsigned int bufferCount, void* buffer, RHI_Buffer_Scope scope);
		bool SetSamplers(unsigned int startSlot, unsigned int samplerCount, void* samplers);
		bool SetTextures(unsigned int startSlot, unsigned int resourceCount, void* shaderResources);	
		bool SetRenderTargets(unsigned int renderTargetCount, void* renderTargets, void* depthStencil);	
		bool SetViewport(const RHI_Viewport& viewport);
		bool SetScissorRectangle(const Math::Rectangle& rectangle);
		//==============================================================================================================

		//= EVENTS ==============================
		void EventBegin(const std::string& name);
		void EventEnd();
		//=======================================

		//= PROFILING ====================================================================
		bool Profiling_CreateQuery(void** buffer, RHI_Query_Type type);
		bool Profiling_QueryStart(void* queryObject);
		bool Profiling_QueryEnd(void* queryObject);
		bool Profiling_GetTimeStamp(void* queryDisjoint);
		float Profiling_GetDuration(void* queryDisjoint, void* queryStart, void* queryEnd);
		//=================================================================================

		//= MISC ==============================================
		bool IsInitialized()	{ return m_initialized; }
		template <typename T>
		T* GetDevice()			{ return (T*)m_device; }
		template <typename T>
		T* GetDeviceContext()	{ return (T*)m_deviceContext; }
		void DetectPrimaryAdapter(RHI_Format format);
		//=====================================================

	private:
		bool m_initialized		= false;
		void* m_device			= nullptr;
		void* m_deviceContext	= nullptr;
	};
}