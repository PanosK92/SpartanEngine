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
#include "RHI_PipelineState.h"
#include "IRHI_Implementation.h"
#include "D3D11\D3D11_InputLayout.h"
#include "RHI_ConstantBuffer.h"
#include "RHI_Sampler.h"
//==================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	RHI_PipelineState::RHI_PipelineState(RHI_Device* rhiDevice)
	{
		m_rhiDevice			= rhiDevice;
		m_primitiveTopology = PrimitiveTopology_NotAssigned;
		m_inputLayout		= Input_NotAssigned;
		m_inputLayoutBuffer = nullptr;
		m_cullMode			= Cull_NotAssigned;
		m_fillMode			= Fill_NotAssigned;
	}

	bool RHI_PipelineState::SetShader(shared_ptr<RHI_Shader>& shader)
	{
		if (!shader)
		{
			LOG_WARNING("RHI_PipelineState::SetShader: Invalid parameter");
			return false;
		}

		// TODO: this has to be done outside of this function 
		SetInputLayout(shader->GetInputLayout());

		m_vertexShader		= shader->GetVertexShaderBuffer();
		m_vertexShaderDirty	= true;

		m_pixelShader		= shader->GetPixelShaderBuffer();
		m_pixelShaderDirty	= true;

		return true;
	}

	bool RHI_PipelineState::SetIndexBuffer(std::shared_ptr<RHI_IndexBuffer>& indexBuffer)
	{
		if (!indexBuffer)
		{
			LOG_WARNING("RHI_PipelineState::SetIndexBuffer: Invalid parameter");
			return false;
		}

		m_indexBuffer		= indexBuffer;
		m_indexBufferDirty	= true;

		return true;
	}

	bool RHI_PipelineState::SetVertexBuffer(shared_ptr<RHI_VertexBuffer>& vertexBuffer)
	{
		if (!vertexBuffer)
		{
			LOG_WARNING("RHI_PipelineState::SetVertexBuffer: Invalid parameter");
			return false;
		}

		m_vertexBuffer		= vertexBuffer;
		m_vertexBufferDirty = true;

		return true;
	}

	bool RHI_PipelineState::SetSampler(shared_ptr<RHI_Sampler>& sampler)
	{
		if (!sampler)
		{
			LOG_WARNING("RHI_PipelineState::SetSampler: Invalid parameter");
			return false;
		}

		m_samplers.emplace_back(sampler->GetBuffer());
		m_samplersDirty = true;

		return true;
	}

	bool RHI_PipelineState::SetTexture(void* shaderResource)
	{
		// allow for null texture to be bound so we can maintain slot order
		m_textures.emplace_back(shaderResource);
		m_texturesDirty	= true;

		return true;
	}

	void RHI_PipelineState::SetConstantBuffer(shared_ptr<RHI_ConstantBuffer>& constantBuffer, unsigned int slot, Buffer_Scope scope)
	{
		m_constantBuffersInfo.emplace_back(constantBuffer, slot, scope);
		m_constantBufferDirty = true;
	}

	void RHI_PipelineState::SetPrimitiveTopology(PrimitiveTopology_Mode primitiveTopology)
	{
		if (m_primitiveTopology == primitiveTopology)
			return;
	
		m_primitiveTopology			= primitiveTopology;
		m_primitiveTopologyDirty	= true;
	}

	bool RHI_PipelineState::SetInputLayout(shared_ptr<D3D11_InputLayout>& inputLayout)
	{
		if (m_inputLayout == inputLayout->GetInputLayout())
			return false;

		m_inputLayout		= inputLayout->GetInputLayout();
		m_inputLayoutBuffer	= inputLayout->GetInputLayoutBuffer();
		m_inputLayoutDirty	= true;

		return true;
	}

	void RHI_PipelineState::SetCullMode(Cull_Mode cullMode)
	{
		if (m_cullMode == cullMode)
			return;

		m_cullMode		= cullMode;
		m_cullModeDirty = true;
	}

	void RHI_PipelineState::SetFillMode(Fill_Mode fillMode)
	{
		if (m_fillMode == fillMode)
			return;

		m_fillMode		= fillMode;
		m_fillModeDirty = true;
	}

	bool RHI_PipelineState::Bind()
	{
		if (!m_rhiDevice)
		{
			LOG_ERROR("IRHI_PipelineState::Bind: Invalid RHI_Device");
			return false;
		}

		// Vertex shader
		if (m_vertexShaderDirty)
		{
			m_rhiDevice->GetDeviceContext()->VSSetShader((ID3D11VertexShader*)m_vertexShader, nullptr, 0);
			Profiler::Get().m_bindVertexShaderCount++;
			m_vertexShaderDirty = false;
		}

		// Pixel shader
		if (m_pixelShaderDirty)
		{
			m_rhiDevice->GetDeviceContext()->PSSetShader((ID3D11PixelShader*)m_pixelShader, nullptr, 0);
			Profiler::Get().m_bindPixelShaderCount++;
			m_pixelShaderDirty = false;
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
		if (m_samplersDirty)
		{
			unsigned int startSlot = 0;
			m_rhiDevice->GetDeviceContext()->PSSetSamplers(startSlot, m_samplers.size(), (ID3D11SamplerState*const*)&m_samplers[0]);
			Profiler::Get().m_bindSamplerCount++;
			m_samplers.clear();
			m_samplers.shrink_to_fit();
			m_samplersDirty	= false;
		}

		// Textures
		if (m_texturesDirty)
		{
			unsigned int startSlot = 0;
			m_rhiDevice->Bind_Textures(startSlot, (unsigned int)m_textures.size(), &m_textures[0]);
			m_textures.clear();
			m_textures.shrink_to_fit();
			Profiler::Get().m_bindTextureCount++;
			m_texturesDirty	= false;
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

		// Constant buffer
		if (m_constantBufferDirty)
		{
			for (const auto& bufferInfo : m_constantBuffersInfo)
			{
				auto d3d11Buffer = (ID3D11Buffer*)bufferInfo.m_constantBuffer->GetBuffer();

				if (bufferInfo.m_scope == Buffer_VertexShader || bufferInfo.m_scope == Buffer_Global)
				{
					m_rhiDevice->GetDeviceContext()->VSSetConstantBuffers(bufferInfo.m_slot, 1, &d3d11Buffer);
					Profiler::Get().m_bindConstantBufferCount++;
				}

				if (bufferInfo.m_scope == Buffer_PixelShader || bufferInfo.m_scope == Buffer_Global)
				{
					m_rhiDevice->GetDeviceContext()->PSSetConstantBuffers(bufferInfo.m_slot, 1, &d3d11Buffer);
					Profiler::Get().m_bindConstantBufferCount++;
				}
			}
			
			m_constantBuffersInfo.clear();
			m_constantBuffersInfo.shrink_to_fit();
			m_constantBufferDirty = false;
		}

		return resultIndexBuffer && resultVertexBuffer;
	}
}