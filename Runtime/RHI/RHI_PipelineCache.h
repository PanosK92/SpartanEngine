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

//= INCLUDES =====================
#include <memory>
#include <unordered_map>
#include "../Math/Rectangle.h"
#include "RHI_Pipeline.h"
#include "RHI_Shader.h"
#include "RHI_Sampler.h"
#include "RHI_Viewport.h"
#include "RHI_SwapChain.h"
#include "RHI_Definition.h"
#include "RHI_BlendState.h"
#include "RHI_InputLayout.h"
#include "RHI_VertexBuffer.h"
#include "RHI_ConstantBuffer.h"
#include "RHI_RasterizerState.h"
#include "RHI_DepthStencilState.h"
//================================

namespace Spartan
{
	class RHI_PipelineState
	{
	public:
		void ComputeHash()
		{
			char buffer[1000];
			sprintf_s
			(
				buffer,
				"%d-%d-%d-%d-%d-%d-%d-%d-%d-%d-%d",
                input_layout        ? input_layout->GetId()         : 0,
                rasterizer_state    ? rasterizer_state->GetId()     : 0,
                blend_state         ? blend_state->GetId()          : 0,
                shader_vertex       ? shader_vertex->GetId()        : 0,
                shader_pixel        ? shader_pixel->GetId()         : 0,
                depth_stencil_state ? depth_stencil_state->GetId()  : 0,
                vertex_buffer       ? vertex_buffer->GetId()        : 0,
                sampler             ? sampler->GetId()              : 0,
                constant_buffer     ? constant_buffer->GetId()      : 0,
                swap_chain          ? swap_chain->GetId()           : 0,
				static_cast<uint32_t>(primitive_topology)
			);

			const std::hash<std::string> hasher;
			m_hash = static_cast<uint32_t>(hasher(buffer));
		}

		auto GetHash() const { return m_hash; }
		bool operator==(const RHI_PipelineState& rhs) const { return GetHash() == rhs.GetHash(); }

		RHI_Shader* shader_vertex						= nullptr;
		RHI_Shader* shader_pixel						= nullptr;
		RHI_InputLayout* input_layout					= nullptr;
		RHI_RasterizerState* rasterizer_state			= nullptr;
		RHI_BlendState* blend_state						= nullptr;
		RHI_DepthStencilState* depth_stencil_state		= nullptr;
		RHI_Sampler* sampler							= nullptr;
		RHI_ConstantBuffer* constant_buffer				= nullptr;
		RHI_VertexBuffer* vertex_buffer					= nullptr;
		RHI_SwapChain* swap_chain						= nullptr;
		RHI_PrimitiveTopology_Mode primitive_topology	= PrimitiveTopology_NotAssigned;
		RHI_Viewport viewport;
		Math::Rectangle scissor;

	private:
		uint32_t m_hash = 0;
	};
}

// Hash function so RHI_PipelineState can be used as key in the unordered map of RHI_PipelineCache
namespace std
{
	template<> struct hash<Spartan::RHI_PipelineState>
	{
		size_t operator()(Spartan::RHI_PipelineState const& state) const noexcept
		{
			return static_cast<size_t>(state.GetHash());
		}
	};
}

namespace Spartan
{
	class RHI_PipelineCache
	{
	public:
		RHI_PipelineCache(const std::shared_ptr<RHI_Device>& rhi_device)
		{
			m_rhi_device = rhi_device;
		}

		auto& GetPipeline(RHI_PipelineState& pipeline_state)
		{
			if (pipeline_state.GetHash() == 0)
			{
				pipeline_state.ComputeHash();
			}

			// If no pipeline exists for this state, create one
			if (m_cache.find(pipeline_state) == m_cache.end())
			{
				m_cache[pipeline_state]; // Create key first so we can pass a reference to each value
				m_cache[pipeline_state] = std::make_shared<RHI_Pipeline>(m_rhi_device, m_cache.find(pipeline_state)->first);
			}

			return m_cache[pipeline_state];
		}

	private:
		std::unordered_map<RHI_PipelineState, std::shared_ptr<RHI_Pipeline>> m_cache;
		std::shared_ptr<RHI_Device> m_rhi_device;
	};
}
