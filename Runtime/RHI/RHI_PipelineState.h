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

#pragma once

//= INCLUDES ======================
#include "RHI_Definition.h"
#include "RHI_Viewport.h"
#include "../Core/Spartan_Object.h"
#include "../Math/Rectangle.h"
#include <array>
//=================================

namespace Spartan
{
    class SPARTAN_CLASS RHI_PipelineState : public Spartan_Object
    {
    public:
        RHI_PipelineState();
        ~RHI_PipelineState() { DestroyFrameResources(); }

        bool IsValid();
        bool AcquireNextImage() const;      
        bool CreateFrameResources(const RHI_Device* rhi_device);
        void* GetFrameBuffer() const;
        void ComputeHash();
        uint32_t GetWidth() const;
        uint32_t GetHeight() const;
        void ResetClearValues();
        auto GetHash()                                  const { return m_hash; }
        void* GetRenderPass()                           const { return m_render_pass; }
        bool operator==(const RHI_PipelineState& rhs)   const { return m_hash == rhs.GetHash(); }

        //= Static, modification can potentially generate a new pipeline =======================================================================================
        RHI_Shader* shader_vertex                                                               = nullptr;
        RHI_Shader* shader_pixel                                                                = nullptr;
        RHI_Shader* shader_compute                                                              = nullptr;
        RHI_RasterizerState* rasterizer_state                                                   = nullptr;
        RHI_BlendState* blend_state                                                             = nullptr;
        RHI_DepthStencilState* depth_stencil_state                                              = nullptr;
        RHI_SwapChain* render_target_swapchain                                                  = nullptr;
        RHI_Texture* render_target_depth_texture                                                = nullptr;
        std::array<RHI_Texture*, state_max_render_target_count> render_target_color_textures    = { nullptr };
        RHI_PrimitiveTopology_Mode primitive_topology                                           = RHI_PrimitiveTopology_Unknown;
        RHI_Viewport viewport                                                                   = RHI_Viewport::Undefined;
        Math::Rectangle scissor                                                                 = Math::Rectangle::Zero;
        bool dynamic_scissor                                                                    = false;
        uint32_t vertex_buffer_stride                                                           = 0;
        uint32_t render_target_color_texture_array_index                                        = 0; // affects render pass, which in turns affects the pipeline
        uint32_t render_target_depth_stencil_texture_array_index                                = 0; // affects render pass, which in turns affects the pipeline
        //======================================================================================================================================================

        //= Dynamic, modification is free ===============================================================================
        RHI_Texture* unordered_access_view         = nullptr;
        bool render_target_depth_texture_read_only = false;    
        int dynamic_constant_buffer_slot           = 3; // such a hack, must fix. Update: Came back to byte me in the ass

        // Clear values
        float clear_depth                                                       = state_depth_dont_care;
        uint8_t clear_stencil                                                   = state_stencil_dont_care;
        std::array<Math::Vector4, state_max_render_target_count> clear_color    = { state_color_dont_care };

        // Profiling
        const char* pass_name   = nullptr;
        bool mark               = false;
        bool profile            = false;
        //===============================================================================================================

    private:
        void DestroyFrameResources();
  
        std::size_t m_hash  = 0;
        void* m_render_pass = nullptr;
        std::array<void*, state_max_render_target_count> m_frame_buffers = { nullptr };

        // Dependencies
        const RHI_Device* m_rhi_device = nullptr;
    };
}
