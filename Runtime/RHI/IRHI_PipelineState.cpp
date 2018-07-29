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
#include "..\Logging\Log.h"
#include "..\Core\Context.h"
#include "IRHI_PipelineState.h"
#include "IRHI_Implementation.h"
#include "IRHI_Shader.h"
#include "D3D11\D3D11_Shader.h"
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

	bool IRHI_PipelineState::SetShader(shared_ptr<IRHI_Shader>& shader)
	{
		if (!m_rhiDevice || !shader)
		{
			LOG_ERROR("RHI_PipelineState::SetVertexShader: Invalid parameter");
			return false;
		}

		if (!SetVertexShader(shader->GetShader()))
			return false;

		if (!SetPixelShader(shader->GetShader()))
			return false;

		return true;
	}

	bool IRHI_PipelineState::SetShader(shared_ptr<D3D11_Shader>& shader)
	{
		if (!m_rhiDevice || !shader)
		{
			LOG_ERROR("RHI_PipelineState::SetVertexShader: Invalid parameters");
			return false;
		}

		if (!SetVertexShader(shader.get()))
			return false;

		if (!SetPixelShader(shader.get()))
			return false;

		return true;
	}

	bool IRHI_PipelineState::SetIndexBuffer(std::shared_ptr<RHI_IndexBuffer>& indexBuffer)
	{
		if (!m_rhiDevice || !indexBuffer)
		{
			LOG_WARNING("RHI_PipelineState::SetIndexBuffer: Invalid parameters.");
			return false;
		}

		Profiler::Get().m_bindBufferIndexCount++;
		return indexBuffer->Bind();
	}

	bool IRHI_PipelineState::SetVertexBuffer(shared_ptr<RHI_VertexBuffer>& vertexBuffer)
	{
		if (!m_rhiDevice || !vertexBuffer)
		{
			LOG_WARNING("RHI_PipelineState::SetVertexBuffer: Invalid parameters.");
			return false;
		}

		Profiler::Get().m_bindBufferVertexCount++;
		return vertexBuffer->Bind();
	}

	bool IRHI_PipelineState::SetSampler(shared_ptr<RHI_Sampler>& sampler, unsigned int slot)
	{
		if (!m_rhiDevice || !sampler)
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

		m_sampler		= sampler.get();
		m_samplerSlot	= slot;
		m_samplerDirty	= true;

		return true;
	}

	bool IRHI_PipelineState::SetVertexShader(D3D11_Shader* shader)
	{
		if (!m_rhiDevice || !shader)
		{
			LOG_ERROR("RHI_PipelineState::SetVertexShader: Invalid parameter");
			return false;
		}

		// TODO: this has to be done outside of this function 
		SetInputLayout(shader->GetInputLayout());

		m_rhiDevice->GetDeviceContext()->VSSetShader(shader->GetVertexShader(), nullptr, 0);

		Profiler::Get().m_bindShaderCount++;

		return true;
	}

	bool IRHI_PipelineState::SetPixelShader(D3D11_Shader* shader)
	{
		if (!m_rhiDevice || !shader)
		{
			LOG_ERROR("RHI_PipelineState::SetPixelShader: Invalid parameter");
			return false;
		}

		m_rhiDevice->GetDeviceContext()->PSSetShader(shader->GetPixelShader(), nullptr, 0);

		Profiler::Get().m_bindShaderCount++;

		return true;
	}

	bool IRHI_PipelineState::SetTextures(vector<void*> shaderResources, unsigned int slot)
	{
		if (!m_rhiDevice || shaderResources.empty())
		{
			LOG_ERROR("RHI_PipelineState::SetPixelShader: Invalid parameters");
			return false;
		}

		m_rhiDevice->Bind_Textures(slot, (unsigned int)shaderResources.size(), &shaderResources[0]);

		return true;
	}

	bool IRHI_PipelineState::SetTexture(void* shaderResource, unsigned int slot)
	{
		if (!m_rhiDevice || !shaderResource)
		{
			LOG_ERROR("RHI_PipelineState::SetPixelShader: Invalid parameters");
			return false;
		}

		m_rhiDevice->Bind_Textures(slot, 1, &shaderResource);

		return true;
	}

	bool IRHI_PipelineState::SetConstantBuffer(std::shared_ptr<RHI_ConstantBuffer>& constantBuffer, unsigned int slot, BufferScope_Mode bufferScope)
	{
		constantBuffer->Bind(bufferScope, slot);
		return true;
	}

	bool IRHI_PipelineState::SetPrimitiveTopology(PrimitiveTopology_Mode primitiveTopology)
	{
		if (m_primitiveTopology == primitiveTopology)
			return false;
	
		m_primitiveTopology			= primitiveTopology;
		m_primitiveTopologyDirty	= true;

		return true;
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

	bool IRHI_PipelineState::SetCullMode(Cull_Mode cullMode)
	{
		if (m_cullMode == cullMode)
			return false;

		m_cullMode		= cullMode;
		m_cullModeDirty = true;

		return true;
	}

	bool IRHI_PipelineState::SetFillMode(Fill_Mode fillMode)
	{
		if (m_fillMode == fillMode)
			return false;

		m_fillMode		= fillMode;
		m_fillModeDirty = true;

		return true;
	}

	bool IRHI_PipelineState::Bind()
	{
		if (!m_rhiDevice)
		{
			LOG_ERROR("RHI_PipelineState::Bind: Invalid RHI_Device");
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
			resultSampler = m_sampler->Bind(m_samplerSlot);
			m_samplerDirty = false;
		}

		return resultSampler;
	}
}