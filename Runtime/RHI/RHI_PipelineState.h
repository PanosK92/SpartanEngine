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
#include <memory>
#include <vector>
//=========================

namespace Directus
{
	class RHI_PipelineState
	{
	public:
		RHI_PipelineState(RHI_Device* rhiDevice);
		~RHI_PipelineState(){}

		// Shader
		bool SetShader(std::shared_ptr<RHI_Shader> shader);
		bool SetShader(std::shared_ptr<D3D11_Shader> shader);
		bool SetVertexShader(D3D11_Shader* shader);
		bool SetPixelShader(D3D11_Shader* shader);

		// Texture
		bool SetTextures(std::vector<void*> shaderResources, unsigned int startSlot);
		bool SetTexture(void* shaderResource, unsigned int startSlot);

		// Sampler
		bool SetSampler(std::shared_ptr<D3D11_Sampler> sampler, unsigned int startSlot);

		// Primitive topology
		bool SetPrimitiveTopology(PrimitiveTopology_Mode primitiveTopology);

		// Input layout
		bool SetInputLayout(std::shared_ptr<D3D11_InputLayout> inputLayout);

		// Cull mode
		bool SetCullMode(Cull_Mode cullMode);

		// Fill mode
		bool SetFillMode(Fill_Mode filleMode);

		// Bind to the GPU
		bool Bind();

	private:
		PrimitiveTopology_Mode m_primitiveTopology;
		bool m_primitiveTopologyIsDirty;

		Input_Layout m_inputLayout;
		void* m_inputLayoutBuffer;
		bool m_inputLayoutIsDirty;

		Cull_Mode m_cullMode;
		bool m_cullModeIsDirty;

		Fill_Mode m_fillMode;
		bool m_fillModeIsDirty;

		RHI_Device* m_rhiDevice;
	};
}