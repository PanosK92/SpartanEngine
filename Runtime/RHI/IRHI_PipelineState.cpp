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

//= INCLUDES =======================
#include "IRHI_PipelineState.h"
#include "IRHI_Implementation.h"
#include "D3D11\D3D11_InputLayout.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	IRHI_PipelineState::IRHI_PipelineState(RHI_Device* rhiDevice)
	{
		m_rhiDevice			= rhiDevice;
		m_primitiveTopology = PrimitiveTopology_NotAssigned;
		m_inputLayout		= Input_NotAssigned;
		m_inputLayoutBuffer = nullptr;
		m_cullMode			= Cull_NotAssigned;
		m_fillMode			= Fill_NotAssigned;
		m_sampler			= nullptr;
		m_samplerSlot		= 0;
	}

	bool IRHI_PipelineState::SetShader(shared_ptr<RHI_Shader>& shader)
	{
		if (!m_rhiDevice || !shader)
		{
			LOG_ERROR("RHI_PipelineState::SetVertexShader: Invalid parameter");
			return false;
		}

		// TODO: this has to be done outside of this function 
		SetInputLayout(shader->GetInputLayout());

		m_rhiDevice->GetDeviceContext()->VSSetShader((ID3D11VertexShader*)shader->GetVertexShaderBuffer(), nullptr, 0);
		Profiler::Get().m_bindVertexShaderCount++;
		m_rhiDevice->GetDeviceContext()->PSSetShader((ID3D11PixelShader*)shader->GetPixelShaderBuffer(), nullptr, 0);
		Profiler::Get().m_bindPixelShaderCount++;

		return true;
	}

	bool IRHI_PipelineState::SetIndexBuffer(std::shared_ptr<RHI_IndexBuffer>& indexBuffer)
	{
		if (!m_rhiDevice || !indexBuffer)
		{
			LOG_WARNING("RHI_PipelineState::SetIndexBuffer: Invalid parameters.");
			return false;
		}

		m_indexBuffer		= indexBuffer;
		m_indexBufferDirty	= true;

		return true;
	}

	bool IRHI_PipelineState::SetVertexBuffer(shared_ptr<RHI_VertexBuffer>& vertexBuffer)
	{
		if (!m_rhiDevice || !vertexBuffer)
		{
			LOG_WARNING("RHI_PipelineState::SetVertexBuffer: Invalid parameters.");
			return false;
		}

		m_vertexBuffer		= vertexBuffer;
		m_vertexBufferDirty = true;

		return true;
	}

	bool IRHI_PipelineState::SetSampler(shared_ptr<RHI_Sampler>& sampler, unsigned int slot)
	{
		if (!sampler)
		{
			LOG_ERROR("RHI_PipelineState::SetSampler: Invalid parameters");
			return false;
		}

		if (m_sampler)
		{
			// If there is already a sampler bound, make sure there new one is different
			if (m_sampler->GetAddressMode()			== sampler->GetAddressMode()		&&
				m_sampler->GetComparisonFunction()	== sampler->GetComparisonFunction() &&
				m_sampler->GetFilter()				== sampler->GetFilter())
			{
				m_samplerDirty	= false;
				return true;
			}
		}

		m_sampler		= sampler;
		m_samplerSlot	= slot;
		m_samplerDirty	= true;

		return true;
	}

	void IRHI_PipelineState::SetTextures(const vector<void*>& shaderResources, unsigned int slot)
	{
		m_textures		= shaderResources;
		m_textureSlot	= slot;
		m_textureDirty	= true;
	}

	void IRHI_PipelineState::SetTextures(void* shaderResource, unsigned int slot)
	{
		m_textures.emplace_back(shaderResource);
		m_textureSlot = slot;
		m_textureDirty = true;
	}

	bool IRHI_PipelineState::SetConstantBuffer(std::shared_ptr<RHI_ConstantBuffer>& constantBuffer, unsigned int slot, BufferScope_Mode bufferScope)
	{
		constantBuffer->Bind(bufferScope, slot);
		Profiler::Get().m_bindConstantBufferCount++;
		return true;
	}

	void IRHI_PipelineState::SetPrimitiveTopology(PrimitiveTopology_Mode primitiveTopology)
	{
		if (m_primitiveTopology == primitiveTopology)
			return;
	
		m_primitiveTopology			= primitiveTopology;
		m_primitiveTopologyDirty	= true;
	}

	bool IRHI_PipelineState::SetInputLayout(shared_ptr<D3D11_InputLayout>& inputLayout)
	{
		if (m_inputLayout == inputLayout->GetInputLayout())
			return false;

		m_inputLayout		= inputLayout->GetInputLayout();
		m_inputLayoutBuffer	= inputLayout->GetInputLayoutBuffer();
		m_inputLayoutDirty	= true;

		return true;
	}

	void IRHI_PipelineState::SetCullMode(Cull_Mode cullMode)
	{
		if (m_cullMode == cullMode)
			return;

		m_cullMode		= cullMode;
		m_cullModeDirty = true;
	}

	void IRHI_PipelineState::SetFillMode(Fill_Mode fillMode)
	{
		if (m_fillMode == fillMode)
			return;

		m_fillMode		= fillMode;
		m_fillModeDirty = true;
	}

	bool IRHI_PipelineState::Bind()
	{
		if (!m_rhiDevice)
		{
			LOG_ERROR("IRHI_PipelineState::Bind: Invalid RHI_Device");
			return false;
		}

		// Primitive topology
		if (m_primitiveTopologyDirty)
		{
			m_rhiDevice->Set_PrimitiveTopology(m_primitiveTopology);
			m_primitiveTopologyDirty = false;
		}

		// Input layout
		if (m_inputLayoutDirty)
		{
			m_rhiDevice->Set_InputLayout(m_inputLayoutBuffer);
			m_inputLayoutDirty = false;
		}

		// Cull mode
		if (m_cullModeDirty)
		{
			m_rhiDevice->Set_CullMode(m_cullMode);
			m_cullModeDirty = false;
		}

		// Fill mode
		if (m_fillModeDirty)
		{
			m_rhiDevice->Set_FillMode(m_fillMode);
			m_fillModeDirty = false;
		}

		// Sampler
		bool resultSampler = true;
		if (m_samplerDirty)
		{
			resultSampler	= m_sampler->Bind(m_samplerSlot);
			Profiler::Get().m_bindSamplerCount++;
			m_sampler		= nullptr;
			m_samplerDirty	= false;
		}

		// Textures
		if (m_textureDirty)
		{
			m_rhiDevice->Bind_Textures(m_textureSlot, (unsigned int)m_textures.size(), &m_textures[0]);
			m_textures.clear();
			m_textures.shrink_to_fit();
			m_textureSlot	= 0;
			m_textureDirty	= false;
		}

		// Index buffer
		bool resultIndexBuffer = true;
		if (m_indexBufferDirty)
		{		
			resultIndexBuffer = m_indexBuffer->Bind();
			Profiler::Get().m_bindBufferIndexCount++;
			m_indexBufferDirty = false;
		}

		// Vertex buffer
		bool resultVertexBuffer = true;
		if (m_vertexBufferDirty)
		{
			resultVertexBuffer = m_vertexBuffer->Bind();
			Profiler::Get().m_bindBufferVertexCount++;
			m_vertexBufferDirty = false;
		}

		return resultSampler && resultIndexBuffer && resultVertexBuffer;
	}
}