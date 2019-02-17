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
#include <memory>
#include <vector>
#include "..\Core\EngineDefs.h"
#include "..\Math\Rectangle.h"
#include "RHI_Definition.h"
#include "RHI_Viewport.h"
//=============================

namespace Directus
{
	class Context;
	class Profiler;
	namespace Math
	{
		class Rectangle;
	}

	struct ConstantBuffer
	{
		ConstantBuffer(void* buffer, unsigned int slot, RHI_Buffer_Scope scope)
		{
			this->buffer	= buffer;
			this->slot		= slot;
			this->scope		= scope;
		}

		void* buffer;
		unsigned int slot;
		RHI_Buffer_Scope scope;
	};

	class ENGINE_CLASS RHI_Pipeline
	{
	public:
		RHI_Pipeline(Context* context, std::shared_ptr<RHI_Device> rhiDevice);
		~RHI_Pipeline(){}

		//= DRAW ======================================================================================
		bool Draw(unsigned int vertexCount);
		bool DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset);
		//=============================================================================================

		//= SET ===========================================================================================================================
		bool SetShader(const std::shared_ptr<RHI_Shader>& shader);
		bool SetVertexShader(const std::shared_ptr<RHI_Shader>& shader);
		bool SetPixelShader(const std::shared_ptr<RHI_Shader>& shader);
		void SetTexture(const std::shared_ptr<RHI_RenderTexture>& texture);
		void SetTexture(const std::shared_ptr<RHI_Texture>& texture);
		void SetTexture(const RHI_Texture* texture);
		void SetTexture(void* texture);
		bool SetDepthStencilState(const std::shared_ptr<RHI_DepthStencilState>& depthStencilState);
		bool SetRasterizerState(const std::shared_ptr<RHI_RasterizerState>& rasterizerState);
		bool SetBlendState(const std::shared_ptr<RHI_BlendState>& blendState);
		bool SetInputLayout(const std::shared_ptr<RHI_InputLayout>& inputLayout);
		bool SetIndexBuffer(const std::shared_ptr<RHI_IndexBuffer>& indexBuffer);
		bool SetVertexBuffer(const std::shared_ptr<RHI_VertexBuffer>& vertexBuffer);
		bool SetSampler(const std::shared_ptr<RHI_Sampler>& sampler);	
		void SetViewport(const RHI_Viewport& viewport);
		void SetScissorRectangle(const Math::Rectangle& rectangle);
		bool SetRenderTarget(const std::shared_ptr<RHI_RenderTexture>& renderTarget, void* depthStencilView = nullptr, bool clear = false);
		bool SetRenderTarget(const std::vector<void*>& renderTargetViews, void* depthStencilView = nullptr, bool clear = false);
		bool SetRenderTarget(void* renderTargetView, void* depthStencilView = nullptr, bool clear = false);
		bool SetConstantBuffer(const std::shared_ptr<RHI_ConstantBuffer>& constantBuffer, unsigned int slot, RHI_Buffer_Scope scope);
		void SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitiveTopology);		
		//=================================================================================================================================

		//= STATES ==
		void Clear();
		bool Bind();
		//===========

	private:
		// Pipeline	
		std::shared_ptr<RHI_InputLayout> m_inputLayout;
		std::shared_ptr<RHI_DepthStencilState> m_depthStencilState;
		std::shared_ptr<RHI_RasterizerState> m_rasterizerState;
		std::shared_ptr<RHI_BlendState> m_blendState;
		std::shared_ptr<RHI_IndexBuffer> m_indexBuffer;
		std::shared_ptr<RHI_VertexBuffer> m_vertexBuffer;
		std::shared_ptr<RHI_Shader> m_vertexShader;
		std::shared_ptr<RHI_Shader> m_pixelShader;
		RHI_Viewport m_viewport;
		Math::Rectangle m_scissorRectangle;
		RHI_PrimitiveTopology_Mode m_primitiveTopology;
		std::vector<ConstantBuffer> m_constantBuffers;
		std::vector<void*> m_samplers;
		std::vector<void*> m_textures;
		std::vector<void*> m_renderTargetViews;	
		void* m_depthStencilView = nullptr;
		bool m_renderTargetsClear;

		// Dirty flags
		bool m_primitiveTopologyDirty	= false;
		bool m_inputLayoutDirty			= false;
		bool m_depthStencilStateDirty	= false;
		bool m_raterizerStateDirty		= false;
		bool m_samplersDirty			= false;
		bool m_texturesDirty			= false;
		bool m_indexBufferDirty			= false;
		bool m_vertexBufferDirty		= false;
		bool m_constantBufferDirty		= false;
		bool m_vertexShaderDirty		= false;
		bool m_pixelShaderDirty			= false;
		bool m_viewportDirty			= false;
		bool m_blendStateDirty			= false;
		bool m_renderTargetsDirty		= false;
		bool m_scissorRectangleDirty	= false;

		// Misc
		std::shared_ptr<RHI_Device> m_rhiDevice;
		Profiler* m_profiler;
	};
}