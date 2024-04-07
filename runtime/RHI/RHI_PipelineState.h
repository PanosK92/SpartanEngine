#/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES ===============
#include "RHI_Definitions.h"
#include <array>
//==========================

namespace Spartan
{
    class SP_CLASS RHI_PipelineState
    {
    public:
        RHI_PipelineState();
        ~RHI_PipelineState();

        void Prepare();
        bool HasClearValues() const;

        uint64_t GetHash()    const { return m_hash; }
        uint32_t GetWidth()   const { return m_width; }
        uint32_t GetHeight()  const { return m_height; }
        bool IsGraphics()     const { return (shader_vertex != nullptr || shader_pixel != nullptr) && !shader_compute; }
        bool IsCompute()      const { return shader_compute != nullptr && !IsGraphics(); }

        //= STATIC - can cause pso generation =============================================
        RHI_Shader* shader_vertex                  = nullptr;
        RHI_Shader* shader_hull                    = nullptr;
        RHI_Shader* shader_domain                  = nullptr;
        RHI_Shader* shader_pixel                   = nullptr;
        RHI_Shader* shader_compute                 = nullptr;
        RHI_RasterizerState* rasterizer_state      = nullptr;
        RHI_BlendState* blend_state                = nullptr;
        RHI_DepthStencilState* depth_stencil_state = nullptr;
        RHI_SwapChain* render_target_swapchain     = nullptr;
        RHI_PrimitiveTopology primitive_toplogy    = RHI_PrimitiveTopology::TriangleList;
        bool instancing                            = false;

        // rt
        std::array<RHI_Texture*, rhi_max_render_target_count> render_target_color_textures;
        RHI_Texture* render_target_depth_texture = nullptr;
        RHI_Texture* vrs_input_texture           = nullptr;
        //=================================================================================

        //= DYNAMIC - will not cause pso generation =================
        uint32_t render_target_color_texture_array_index         = 0;
        uint32_t render_target_depth_stencil_texture_array_index = 0;

        float clear_depth      = rhi_depth_load;
        uint32_t clear_stencil = rhi_stencil_load;
        std::array<Color, rhi_max_render_target_count> clear_color;

        std::string name;
        bool resolution_scale = false;
        //===========================================================

    private:
        uint32_t m_width                  = 0;
        uint32_t m_height                 = 0;
        uint64_t m_hash                   = 0;
        RHI_PipelineState* m_pso_previous = nullptr;
    };
}
