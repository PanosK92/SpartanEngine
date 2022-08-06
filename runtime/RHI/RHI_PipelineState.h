/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "RHI_Definition.h"
#include "RHI_Viewport.h"
#include "../Core/SpartanObject.h"
#include "../Math/Rectangle.h"
#include <array>
//================================

namespace Spartan
{
    class SPARTAN_CLASS RHI_PipelineState : public SpartanObject
    {
    public:
        RHI_PipelineState();
        ~RHI_PipelineState();

        bool IsValid();
        uint32_t GetWidth() const;
        uint32_t GetHeight() const;
        uint64_t ComputeHash() const;
        bool HasClearValues();
        bool IsGraphics() const { return (shader_vertex != nullptr || shader_pixel != nullptr) && !shader_compute; }
        bool IsCompute()  const { return shader_compute != nullptr && !IsGraphics(); }

        //= Static, modification can potentially generate a new pipeline =====================
        RHI_Shader* shader_vertex                     = nullptr;
        RHI_Shader* shader_pixel                      = nullptr;
        RHI_Shader* shader_compute                    = nullptr;
        RHI_RasterizerState* rasterizer_state         = nullptr;
        RHI_BlendState* blend_state                   = nullptr;
        RHI_DepthStencilState* depth_stencil_state    = nullptr;
        RHI_SwapChain* render_target_swapchain        = nullptr;
        RHI_PrimitiveTopology_Mode primitive_topology = RHI_PrimitiveTopology_Mode::Undefined;
        RHI_Viewport viewport                         = RHI_Viewport::Undefined;
        Math::Rectangle scissor                       = Math::Rectangle::Zero;
        bool dynamic_scissor                          = false;
        bool can_use_vertex_index_buffers             = true;

        // RTs
        RHI_Texture* render_target_depth_texture = nullptr;
        std::array<RHI_Texture*, rhi_max_render_target_count> render_target_color_textures;

        // RT indices (affect render pass)
        uint32_t render_target_color_texture_array_index = 0;
        uint32_t render_target_depth_stencil_texture_array_index = 0;

        // Clear values
        float clear_depth      = rhi_depth_load;
        uint32_t clear_stencil = rhi_stencil_load;
        std::array<Math::Vector4, rhi_max_render_target_count> clear_color;
        //====================================================================================

        //= Dynamic, modification wont' create a new pipeline =
        bool render_target_depth_texture_read_only = false;
        //=====================================================

    private:
        const RHI_Device* m_rhi_device = nullptr;
    };
}
