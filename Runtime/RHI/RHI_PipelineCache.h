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
        RHI_PipelineState() { Clear(); }

        void ComputeHash()
        {
            static std::hash<uint32_t> hasher;

            m_hash = m_hash * 31 + static_cast<uint32_t>(hasher(input_layout->GetId()));
            m_hash = m_hash * 31 + static_cast<uint32_t>(hasher(rasterizer_state->GetId()));
            m_hash = m_hash * 31 + static_cast<uint32_t>(hasher(blend_state->GetId()));
            m_hash = m_hash * 31 + static_cast<uint32_t>(hasher(shader_vertex->GetId()));
            m_hash = m_hash * 31 + static_cast<uint32_t>(hasher(shader_pixel->GetId()));
            m_hash = m_hash * 31 + static_cast<uint32_t>(hasher(depth_stencil_state->GetId()));
            m_hash = m_hash * 31 + static_cast<uint32_t>(hasher(vertex_buffer->GetId()));
            m_hash = m_hash * 31 + static_cast<uint32_t>(hasher(static_cast<uint32_t>(primitive_topology)));
        }

        auto GetHash() const { return m_hash; }
        bool operator==(const RHI_PipelineState& rhs) const { return GetHash() == rhs.GetHash(); }

        void Clear()
        {
            shader_vertex       = nullptr;
            shader_pixel        = nullptr;
            input_layout        = nullptr;
            rasterizer_state    = nullptr;
            blend_state         = nullptr;
            depth_stencil_state = nullptr;
            vertex_buffer       = nullptr;
            primitive_topology  = RHI_PrimitiveTopology_Unknown;
            viewport            = RHI_Viewport(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            scissor             = Math::Rectangle(0.0f, 0.0f, 0.0f, 0.0f);
        }

        RHI_Shader* shader_vertex;
        RHI_Shader* shader_pixel;
        RHI_InputLayout* input_layout;
        RHI_RasterizerState* rasterizer_state;
        RHI_BlendState* blend_state;
        RHI_DepthStencilState* depth_stencil_state;
        RHI_VertexBuffer* vertex_buffer;
        RHI_PrimitiveTopology_Mode primitive_topology;
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
