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

//= INCLUDES =====================
#include "RHI_Pipeline.h"
#include "RHI_Implementation.h"
#include "RHI_Sampler.h"
#include "RHI_Device.h"
#include "RHI_RenderTexture.h"
#include "RHI_VertexBuffer.h"
#include "RHI_IndexBuffer.h"
#include "RHI_Texture.h"
#include "RHI_Shader.h"
#include "RHI_ConstantBuffer.h"
#include "RHI_InputLayout.h"
#include "RHI_DepthStencilState.h"
#include "RHI_RasterizerState.h"
#include "RHI_BlendState.h"
#include "..\Logging\Log.h"
#include "../Profiling/Profiler.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	RHI_Pipeline::RHI_Pipeline(Context* context, shared_ptr<RHI_Device> rhiDevice)
	{
		m_rhiDevice	= rhiDevice;
		m_profiler	= context->GetSubsystem<Profiler>().get();
	}

	bool RHI_Pipeline::DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset)
	{
		bool bindResult = Bind();
		m_rhiDevice->DrawIndexed(indexCount, indexOffset, vertexOffset);
		m_profiler->m_rhiDrawCalls++;
		return bindResult;
	}

	bool RHI_Pipeline::Draw(unsigned int vertexCount)
	{
		bool bindResult = Bind();
		m_rhiDevice->Draw(vertexCount);
		m_profiler->m_rhiDrawCalls++;
		return bindResult;
	}

	bool RHI_Pipeline::SetShader(const shared_ptr<RHI_Shader>& shader)
	{
		return SetVertexShader(shader) && SetPixelShader(shader);
	}

	bool RHI_Pipeline::SetVertexShader(const shared_ptr<RHI_Shader>& shader)
	{
		if (!shader || !shader->HasVertexShader())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_vertexShader)
		{
			if (m_vertexShader->RHI_GetID() == shader->RHI_GetID())
				return true;
		}

		SetInputLayout(shader->GetInputLayout()); // TODO: this has to be done outside of this function 
		m_vertexShader		= shader;
		m_vertexShaderDirty = true;

		return true;
	}

	bool RHI_Pipeline::SetPixelShader(const shared_ptr<RHI_Shader>& shader)
	{
		if (!shader || !shader->HasPixelShader())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_pixelShader)
		{
			if (m_pixelShader->RHI_GetID() == shader->RHI_GetID())
				return true;
		}

		m_pixelShader		= shader;
		m_pixelShaderDirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetIndexBuffer(const shared_ptr<RHI_IndexBuffer>& indexBuffer)
	{
		if (!indexBuffer)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_indexBuffer		= indexBuffer;
		m_indexBufferDirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetVertexBuffer(const shared_ptr<RHI_VertexBuffer>& vertexBuffer)
	{
		if (!vertexBuffer)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_vertexBuffer		= vertexBuffer;
		m_vertexBufferDirty = true;

		return true;
	}

	bool RHI_Pipeline::SetSampler(const shared_ptr<RHI_Sampler>& sampler)
	{
		m_samplers.emplace_back(sampler ? sampler->GetBuffer() : nullptr);
		m_samplersDirty = true;

		return true;
	}

	void RHI_Pipeline::SetTexture(const shared_ptr<RHI_RenderTexture>& texture)
	{
		// allow for null texture to be bound so we can maintain slot order
		m_textures.emplace_back(texture ? texture->GetShaderResource() : nullptr);
		m_texturesDirty = true;
	}

	void RHI_Pipeline::SetTexture(const shared_ptr<RHI_Texture>& texture)
	{
		// allow for null texture to be bound so we can maintain slot order
		m_textures.emplace_back(texture ? texture->GetShaderResource() : nullptr);
		m_texturesDirty = true;
	}

	void RHI_Pipeline::SetTexture(const RHI_Texture* texture)
	{
		// allow for null texture to be bound so we can maintain slot order
		m_textures.emplace_back(texture ? texture->GetShaderResource() : nullptr);
		m_texturesDirty = true;
	}

	void RHI_Pipeline::SetTexture(void* texture)
	{
		m_textures.emplace_back(texture);
		m_texturesDirty = true;
	}

	bool RHI_Pipeline::SetRenderTarget(const shared_ptr<RHI_RenderTexture>& renderTarget, void* depthStencilView /*= nullptr*/, bool clear /*= false*/)
	{
		if (!renderTarget)
			return false;

		m_renderTargetViews.clear();
		m_renderTargetViews.emplace_back(renderTarget->GetRenderTargetView());
		m_depthStencilView		= depthStencilView;
		m_renderTargetsClear	= clear;
		m_renderTargetsDirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetRenderTarget(void* renderTargetView, void* depthStencilView /*= nullptr*/, bool clear /*= false*/)
	{
		if (!renderTargetView)
			return false;

		m_renderTargetViews.clear();
		m_renderTargetViews.emplace_back(renderTargetView);

		m_depthStencilView			= depthStencilView;
		m_renderTargetsClear	= clear;
		m_renderTargetsDirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetRenderTarget(const vector<void*>& renderTargetViews, void* depthStencilView /*= nullptr*/, bool clear /*= false*/)
	{
		if (renderTargetViews.empty())
			return false;

		m_renderTargetViews.clear();
		for (const auto& renderTarget : renderTargetViews)
		{
			if (!renderTarget)
				continue;

			m_renderTargetViews.emplace_back(renderTarget);
		}

		m_depthStencilView		= depthStencilView;
		m_renderTargetsClear	= clear;
		m_renderTargetsDirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetConstantBuffer(const shared_ptr<RHI_ConstantBuffer>& constantBuffer, unsigned int slot, RHI_Buffer_Scope scope)
	{
		auto bufferPtr = constantBuffer ? constantBuffer->GetBuffer() : nullptr;
		m_constantBuffers.emplace_back(bufferPtr, slot, scope);
		m_constantBufferDirty = true;

		return true;
	}

	void RHI_Pipeline::SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitiveTopology)
	{
		if (m_primitiveTopology == primitiveTopology)
			return;
	
		m_primitiveTopology			= primitiveTopology;
		m_primitiveTopologyDirty	= true;
	}

	bool RHI_Pipeline::SetInputLayout(const shared_ptr<RHI_InputLayout>& inputLayout)
	{
		if (!inputLayout)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_inputLayout)
		{
			if (m_inputLayout->GetInputLayout() == inputLayout->GetInputLayout())
				return true;
		}

		m_inputLayout		= inputLayout;
		m_inputLayoutDirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetDepthStencilState(const std::shared_ptr<RHI_DepthStencilState>& depthStencilState)
	{
		if (!depthStencilState)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_depthStencilState)
		{
			if (m_depthStencilState->GetDepthEnabled() == depthStencilState->GetDepthEnabled())
				return true;
		}

		m_depthStencilState			= depthStencilState;
		m_depthStencilStateDirty	= true;
		return true;
	}

	bool RHI_Pipeline::SetRasterizerState(const std::shared_ptr<RHI_RasterizerState>& rasterizerState)
	{
		if (!rasterizerState)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_rasterizerState)
		{
			bool equal =
				m_rasterizerState->GetCullMode()				== rasterizerState->GetCullMode()			&&
				m_rasterizerState->GetFillMode()				== rasterizerState->GetFillMode()			&&
				m_rasterizerState->GetDepthClipEnabled()		== rasterizerState->GetDepthClipEnabled()	&&
				m_rasterizerState->GetScissorEnabled()			== rasterizerState->GetScissorEnabled()	&&
				m_rasterizerState->GetMultiSampleEnabled()		== rasterizerState->GetMultiSampleEnabled() &&
				m_rasterizerState->GetAntialisedLineEnabled()	== rasterizerState->GetAntialisedLineEnabled();

			if (equal)
				return true;
		}

		m_rasterizerState		= rasterizerState;
		m_raterizerStateDirty	= true;
		return true;
	}

	bool RHI_Pipeline::SetBlendState(const std::shared_ptr<RHI_BlendState>& blendState)
	{
		if (!blendState)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_blendState)
		{
			if (m_blendState->BlendEnabled() == blendState->BlendEnabled())
				return true;
		}

		m_blendState		= blendState;
		m_blendStateDirty	= true;
		return true;
	}

	void RHI_Pipeline::SetViewport(const RHI_Viewport& viewport)
	{
		if (viewport == m_viewport)
			return;

		m_viewport		= viewport;
		m_viewportDirty = true;
	}

	void RHI_Pipeline::SetScissorRectangle(const Math::Rectangle& rectangle)
	{
		if (m_scissorRectangle == rectangle)
			return;

		m_scissorRectangle		= rectangle;
		m_scissorRectangleDirty = true;
	}

	bool RHI_Pipeline::Bind()
	{
		if (!m_rhiDevice)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// Render Targets
		if (m_renderTargetsDirty)
		{
			if (m_renderTargetViews.empty())
			{
				LOG_ERROR("Invalid render target(s)");
				return false;
			}

			m_rhiDevice->SetRenderTargets((unsigned int)m_renderTargetViews.size(), &m_renderTargetViews[0], m_depthStencilView);
			m_profiler->m_rhiBindingsRenderTarget++;

			if (m_renderTargetsClear)
			{
				for (const auto& renderTargetView : m_renderTargetViews)
				{
					m_rhiDevice->ClearRenderTarget(renderTargetView, Vector4(0, 0, 0, 0));
				}

				if (m_depthStencilView)
				{
					float depth = m_viewport.GetMaxDepth();
					#if REVERSE_Z == 1
					depth = 1.0f - depth;
					#endif

					m_rhiDevice->ClearDepthStencil(m_depthStencilView, Clear_Depth, depth, 0);
				}
			}

			m_renderTargetsClear = false;
			m_renderTargetsDirty = false;
		}

		// Textures
		if (m_texturesDirty)
		{
			unsigned int startSlot = 0;
			unsigned int textureCount = (unsigned int)m_textures.size();
			void* textures = textureCount != 0 ? &m_textures[0] : nullptr;

			m_rhiDevice->SetTextures(startSlot, textureCount, textures);
			m_profiler->m_rhiBindingsTexture++;

			m_textures.clear();
			m_texturesDirty = false;
		}

		// Samplers
		if (m_samplersDirty)
		{
			unsigned int startSlot		= 0;
			unsigned int samplerCount	= (unsigned int)m_samplers.size();
			void* samplers				= samplerCount != 0 ? &m_samplers[0] : nullptr;

			m_rhiDevice->SetSamplers(startSlot, samplerCount, samplers);
			m_profiler->m_rhiBindingsSampler++;

			m_samplers.clear();
			m_samplersDirty = false;
		}

		// Constant buffers
		if (m_constantBufferDirty)
		{
			for (const auto& constantBuffer : m_constantBuffers)
			{
				m_rhiDevice->SetConstantBuffers(constantBuffer.slot, 1, (void*)&constantBuffer.buffer, constantBuffer.scope);
				m_profiler->m_rhiBindingsBufferConstant += (constantBuffer.scope == Buffer_Global) ? 2 : 1;
			}

			m_constantBuffers.clear();
			m_constantBufferDirty = false;
		}

		// Vertex shader
		if (m_vertexShaderDirty)
		{
			m_rhiDevice->SetVertexShader(m_vertexShader);
			m_profiler->m_rhiBindingsVertexShader++;
			m_vertexShaderDirty = false;
		}

		// Pixel shader
		if (m_pixelShaderDirty)
		{
			m_rhiDevice->SetPixelShader(m_pixelShader);
			m_profiler->m_rhiBindingsPixelShader++;
			m_pixelShaderDirty = false;
		}

		// Input layout
		if (m_inputLayoutDirty)
		{
			m_rhiDevice->SetInputLayout(m_inputLayout);
			m_inputLayoutDirty = false;
		}

		// Viewport
		if (m_viewportDirty)
		{
			m_rhiDevice->SetViewport(m_viewport);
			m_viewportDirty = false;
		}

		if (m_scissorRectangleDirty)
		{
			m_rhiDevice->SetScissorRectangle(m_scissorRectangle);
			m_scissorRectangleDirty = false;
		}

		// Primitive topology
		if (m_primitiveTopologyDirty)
		{
			m_rhiDevice->SetPrimitiveTopology(m_primitiveTopology);
			m_primitiveTopologyDirty = false;
		}

		// Depth-Stencil state
		if (m_depthStencilStateDirty)
		{
			m_rhiDevice->SetDepthStencilState(m_depthStencilState);
			m_depthStencilStateDirty = false;
		}

		// Rasterizer state
		if (m_raterizerStateDirty)
		{
			m_rhiDevice->SetRasterizerState(m_rasterizerState);
			m_raterizerStateDirty = false;
		}

		// Index buffer
		bool resultIndexBuffer = false;
		if (m_indexBufferDirty)
		{
			resultIndexBuffer = m_rhiDevice->SetIndexBuffer(m_indexBuffer);
			m_profiler->m_rhiBindingsBufferIndex++;
			m_indexBufferDirty = false;
		}

		// Vertex buffer
		bool resultVertexBuffer = false;
		if (m_vertexBufferDirty)
		{
			resultVertexBuffer = m_rhiDevice->SetVertexBuffer(m_vertexBuffer);
			m_profiler->m_rhiBindingsBufferVertex++;
			m_vertexBufferDirty = false;
		}

		// Blend state
		bool result_blendState = false;
		if (m_blendStateDirty)
		{
			result_blendState = m_rhiDevice->SetBlendState(m_blendState);
			m_blendStateDirty = false;
		}

		return resultIndexBuffer && resultVertexBuffer && result_blendState;
	}

	void RHI_Pipeline::Clear()
	{
		vector<void*> empty(30);
		void* empyt_ptr	= &empty[0];
		
		// Render targets
		m_rhiDevice->SetRenderTargets(8, empyt_ptr, nullptr);
		m_renderTargetViews.clear();
		m_depthStencilView		= nullptr;
		m_renderTargetsClear	= false;
		m_renderTargetsDirty	= false;

		// Textures
		m_rhiDevice->SetTextures(0, 20, empyt_ptr);
		m_textures.clear();
		m_texturesDirty = false;

		// Samplers
		m_rhiDevice->SetSamplers(0, 10, empyt_ptr);
		m_samplers.clear();
		m_samplersDirty = false;

		// Constant buffers
		m_rhiDevice->SetConstantBuffers(0, 10, empyt_ptr, Buffer_Global);
		m_constantBuffers.clear();
		m_constantBufferDirty = false;
	}
}