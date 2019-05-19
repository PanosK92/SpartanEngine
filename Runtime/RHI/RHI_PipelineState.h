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
#include "../Math/Rectangle.h"
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
		bool operator==(const RHI_PipelineState& rhs) const
		{
			return 
				shader_vertex->RHI_GetID()			== rhs.shader_vertex->RHI_GetID()		&&
				shader_pixel->RHI_GetID()			== rhs.shader_pixel->RHI_GetID()		&&
				input_layout						== rhs.input_layout						&&
				rasterizer_state					== rhs.rasterizer_state					&&
				blend_state							== rhs.blend_state						&&
				depth_stencil_state->RHI_GetID()	== rhs.depth_stencil_state->RHI_GetID()	&&
				sampler->RHI_GetID()				== rhs.sampler->RHI_GetID()				&&
				constant_buffer->RHI_GetID()		== rhs.constant_buffer->RHI_GetID()		&&
				vertex_buffer->RHI_GetID()			== rhs.vertex_buffer->RHI_GetID()		&&
				primitive_topology					== rhs.primitive_topology;
		}

		RHI_Shader* shader_vertex						= nullptr;
		RHI_Shader* shader_pixel						= nullptr;
		RHI_InputLayout* input_layout					= nullptr;
		RHI_RasterizerState* rasterizer_state			= nullptr;
		RHI_BlendState* blend_state						= nullptr;
		RHI_DepthStencilState* depth_stencil_state		= nullptr;		
		RHI_Sampler* sampler							= nullptr;
		RHI_ConstantBuffer* constant_buffer				= nullptr;
		RHI_VertexBuffer* vertex_buffer					= nullptr;		
		RHI_PrimitiveTopology_Mode primitive_topology	= PrimitiveTopology_NotAssigned;
		RHI_Viewport viewport;
		Math::Rectangle scissor;
	};
}

// Hash function so RHI_PipelineState can be used as key
namespace std
{
	template<> struct hash<Spartan::RHI_PipelineState>
	{
		size_t operator()(Spartan::RHI_PipelineState const& state) const noexcept
		{
			return size_t(state.blend_state->RHI_GetID());
		}
	};
}