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

        //= State (things that if changed, will cause a new pipeline to be generated) ==============================
        RHI_Shader* shader_vertex                                                   = nullptr; 
        RHI_RasterizerState* rasterizer_state                                       = nullptr;
        RHI_BlendState* blend_state                                                 = nullptr;
        RHI_DepthStencilState* depth_stencil_state                                  = nullptr;
        RHI_SwapChain* render_target_swapchain                                      = nullptr;
        RHI_PrimitiveTopology_Mode primitive_topology                               = RHI_PrimitiveTopology_Unknown;
        RHI_Viewport viewport                                                       = RHI_Viewport::Undefined;
        Math::Rectangle scissor                                                     = Math::Rectangle::Zero;
        uint32_t vertex_buffer_stride                                               = 0;
        RHI_Texture* render_target_depth_texture                                    = nullptr;    
        RHI_Texture* render_target_color_textures[state_max_render_target_count]    = { nullptr };
        bool dynamic_scissor                                                        = false;
        //==========================================================================================================
        RHI_Shader* shader_pixel                                                    = nullptr;
        RHI_Shader* shader_compute                                                  = nullptr;
        RHI_Texture* unordered_access_view                                          = nullptr;
        bool render_target_depth_texture_read_only                                  = false;

        // Texture array indices
        uint32_t render_target_color_texture_array_index            = 0;
        uint32_t render_target_depth_stencil_texture_array_index    = 0;

        // Dynamic constant buffers
        int dynamic_constant_buffer_slot = 2; // such a hack, must fix

        // Clear values
        float clear_depth                                         = state_dont_clear_depth;
        uint8_t clear_stencil                                     = state_dont_clear_stencil;
        Math::Vector4 clear_color[state_max_render_target_count]  = { state_dont_clear_color };

        // Profiling
        const char* pass_name   = nullptr;
        bool mark               = false;
        bool profile            = false;

    private:
        void DestroyFrameResources();
  
        std::size_t m_hash          = 0;
        void* m_render_pass         = nullptr;
        void* m_frame_buffers[state_max_render_target_count];

        // Dependencies
        const RHI_Device* m_rhi_device = nullptr;
    };
}
