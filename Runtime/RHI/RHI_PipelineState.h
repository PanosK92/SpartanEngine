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

//= INCLUDES ==================
#include <memory>
#include <vector>
#include "..\Core\EngineDefs.h"
#include "RHI_Definition.h"
#include "RHI_Viewport.h"
//=============================

namespace Directus
{
	struct ConstantBufferInfo
	{
		ConstantBufferInfo(void* buffer, unsigned int slot, Buffer_Scope scope)
		{
			m_buffer	= buffer;
			m_slot		= slot;
			m_scope		= scope;
		}
		void* m_buffer;
		unsigned int m_slot;
		Buffer_Scope m_scope;
	};

	class ENGINE_CLASS RHI_PipelineState
	{
	public:
		RHI_PipelineState(std::shared_ptr<RHI_Device> rhiDevice);
		~RHI_PipelineState(){}

		// Shader
		bool SetShader(std::shared_ptr<RHI_Shader>& shader);

		// Texture
		bool SetTexture(const std::shared_ptr<RHI_RenderTexture>& texture);
		bool SetTexture(const std::shared_ptr<RHI_Texture>& texture);

		// Render texture
		bool SetRenderTexture(const std::shared_ptr<RHI_RenderTexture>& renderTexture, bool clear = false);
		// Render targets
		void SetRenderTargets(const std::vector<void*>& renderTargets, void* depthStencil);

		// Constant, vertex & index buffers
		void SetConstantBuffer(const std::shared_ptr<RHI_ConstantBuffer>& constantBuffer, unsigned int slot, Buffer_Scope scope);
		bool SetIndexBuffer(const std::shared_ptr<RHI_IndexBuffer>& indexBuffer);
		bool SetVertexBuffer(const std::shared_ptr<RHI_VertexBuffer>& vertexBuffer);
		
		// Sampler
		bool SetSampler(const std::shared_ptr<RHI_Sampler>& sampler);

		// Primitive topology
		void SetPrimitiveTopology(PrimitiveTopology_Mode primitiveTopology);

		// Input layout
		bool SetInputLayout(const std::shared_ptr<RHI_InputLayout>& inputLayout);

		// Cull mode
		void SetCullMode(Cull_Mode cullMode);

		// Fill mode
		void SetFillMode(Fill_Mode filleMode);

		// Viewport
		void SetViewport(float width, float height);
		void SetViewport(const RHI_Viewport& viewport);

		// Bind to the GPU
		bool Bind();

	private:
		// Primitive topology
		PrimitiveTopology_Mode m_primitiveTopology;
		bool m_primitiveTopologyDirty;

		// Input layout
		Input_Layout m_inputLayout;
		void* m_inputLayoutBuffer;
		bool m_inputLayoutDirty;

		// Cull mode
		Cull_Mode m_cullMode;
		bool m_cullModeDirty;

		// Fill mode
		Fill_Mode m_fillMode;
		bool m_fillModeDirty;

		// Samplers
		std::vector<void*> m_samplers;
		bool m_samplersDirty;

		// Textures
		std::vector<void*> m_textures;
		bool m_texturesDirty;

		// Index buffer
		std::shared_ptr<RHI_IndexBuffer> m_indexBuffer;
		bool m_indexBufferDirty;

		// Vertex buffer
		std::shared_ptr<RHI_VertexBuffer> m_vertexBuffer;
		bool m_vertexBufferDirty;

		// Constant buffers
		std::vector<ConstantBufferInfo> m_constantBuffersInfo;
		bool m_constantBufferDirty;

		// Vertex shader
		void* m_vertexShader;
		bool m_vertexShaderDirty;

		// Pixel Shader
		void* m_pixelShader;
		bool m_pixelShaderDirty;

		// Viewport
		RHI_Viewport m_viewport;
		bool m_viewportDirty;

		// Render targets
		std::shared_ptr<RHI_RenderTexture> m_renderTexture;
		bool m_renderTextureClear;
		bool m_renderTextureDirty;

		// Render targets
		std::vector<void*> m_renderTargets;
		void* m_depthStencil;
		bool m_renderTargetDirty;

		// Device
		std::shared_ptr<RHI_Device> m_rhiDevice;
	};
}