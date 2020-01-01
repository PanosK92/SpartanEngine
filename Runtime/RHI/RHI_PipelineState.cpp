/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "RHI_PipelineState.h"
#include "RHI_Shader.h"
#include "RHI_Texture.h"
#include "RHI_SwapChain.h"
#include "RHI_BlendState.h"
#include "RHI_InputLayout.h"
#include "RHI_RasterizerState.h"
#include "RHI_DepthStencilState.h"
#include "..\Utilities\Hash.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{

    RHI_PipelineState::RHI_PipelineState()
    {
        for (uint32_t i = 0; i < state_max_render_target_count; i++)
        {
            render_target_color_textures[i] = nullptr;
            render_target_color_clear[i]    = state_dont_clear_color;
            m_frame_buffers[i]              = nullptr;
        }
    }

    bool RHI_PipelineState::IsValid()
    {
        // Ensure that only one render target is active at a time
        if (render_target_swapchain && render_target_color_textures[0])
            return false;

        // Ensure that the required members are set
        return  shader_vertex       != nullptr &&
                rasterizer_state    != nullptr &&
                blend_state         != nullptr &&
                depth_stencil_state != nullptr &&
                primitive_topology  != RHI_PrimitiveTopology_Unknown &&
                (render_target_swapchain != nullptr || render_target_color_textures[0] != nullptr || render_target_depth_texture != nullptr);

        // Notes
        // - Pixel shader can be null
        // - Viewport and scissor can be undefined as they can also be set dynamically
    }

    bool RHI_PipelineState::AcquireNextImage()
    {
        return render_target_swapchain ? render_target_swapchain->AcquireNextImage() : true;
    }

	uint32_t RHI_PipelineState::GetWidth() const
	{
        if (render_target_swapchain)
            return render_target_swapchain->GetWidth();

        if (render_target_color_textures[0])
            return render_target_color_textures[0]->GetWidth();

        return 0;
	}

    uint32_t RHI_PipelineState::GetHeight() const
    {
        if (render_target_swapchain)
            return render_target_swapchain->GetHeight();

        if (render_target_color_textures[0])
            return render_target_color_textures[0]->GetHeight();

        return 0;
    }

	void RHI_PipelineState::ComputeHash()
    {
        m_hash = 0;

        Utility::Hash::hash_combine(m_hash, scissor.x);
        Utility::Hash::hash_combine(m_hash, scissor.y);
        Utility::Hash::hash_combine(m_hash, scissor.width);
        Utility::Hash::hash_combine(m_hash, scissor.height);
        Utility::Hash::hash_combine(m_hash, viewport.x);
        Utility::Hash::hash_combine(m_hash, viewport.y);
        Utility::Hash::hash_combine(m_hash, viewport.width);
        Utility::Hash::hash_combine(m_hash, viewport.height);
        Utility::Hash::hash_combine(m_hash, primitive_topology);
        Utility::Hash::hash_combine(m_hash, scissor_dynamic);
        Utility::Hash::hash_combine(m_hash, vertex_buffer_stride);
        Utility::Hash::hash_combine(m_hash, rasterizer_state->GetId());
        Utility::Hash::hash_combine(m_hash, blend_state->GetId());
        Utility::Hash::hash_combine(m_hash, depth_stencil_state->GetId());
        Utility::Hash::hash_combine(m_hash, shader_vertex->GetId());

        if (shader_pixel)
        {
            Utility::Hash::hash_combine(m_hash, shader_pixel->GetId());
        }

        for (auto i = 0; i < state_max_render_target_count; i++)
        {
            if (render_target_color_textures[i])
            {
                Utility::Hash::hash_combine(m_hash, render_target_color_textures[i]->GetId());
            }
        }

        if (render_target_depth_texture)
        {
            Utility::Hash::hash_combine(m_hash, render_target_depth_texture->GetId());
        }
    }
}
