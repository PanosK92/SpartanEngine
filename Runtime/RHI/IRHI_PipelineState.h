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
#include "IRHI_Definition.h"
#include "..\Core\EngineDefs.h"
//=============================

namespace Directus
{
	class ENGINE_CLASS IRHI_PipelineState
	{
	public:
		IRHI_PipelineState(RHI_Device* rhiDevice);
		~IRHI_PipelineState(){}

		// Shader
		bool SetShader(std::shared_ptr<RHI_Shader>& shader);

		// Texture
		void SetTextures(const std::vector<void*>& shaderResources, unsigned int slot);
		void SetTextures(void* shaderResource, unsigned int slot);

		// Constant, vertex & index buffers
		bool SetConstantBuffer(std::shared_ptr<RHI_ConstantBuffer>& constantBuffer, unsigned int slot, BufferScope_Mode bufferScope);
		bool SetIndexBuffer(std::shared_ptr<RHI_IndexBuffer>& indexBuffer);
		bool SetVertexBuffer(std::shared_ptr<RHI_VertexBuffer>& vertexBuffer);

		// Sampler
		bool SetSampler(std::shared_ptr<RHI_Sampler>& sampler, unsigned int slot);

		// Primitive topology
		void SetPrimitiveTopology(PrimitiveTopology_Mode primitiveTopology);

		// Input layout
		bool SetInputLayout(std::shared_ptr<D3D11_InputLayout>& inputLayout);

		// Cull mode
		void SetCullMode(Cull_Mode cullMode);

		// Fill mode
		void SetFillMode(Fill_Mode filleMode);

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

		// Sampler
		RHI_Sampler* m_sampler;
		unsigned int m_samplerSlot;
		bool m_samplerDirty;

		// Textures
		std::vector<void*> m_textures;
		unsigned int m_textureSlot;
		bool m_textureDirty;

		RHI_Device* m_rhiDevice;
	};
}