/*
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

//= INCLUDES =================================
#include <array>
#include <atomic>
#include "RHI_Definitions.h"
#include "RHI_PipelineState.h"
#include "RHI_Descriptor.h"
#include "../Rendering/Renderer_Definitions.h"
//============================================

namespace Spartan
{
    // forward declarations
    namespace Math { class Rectangle; }

    enum class RHI_CommandListState : uint8_t
    {
        Idle,
        Recording,
        Submitted
    };

    class SP_CLASS RHI_CommandList
    {
    public:
        RHI_CommandList(void* cmd_pool, const char* name);
        ~RHI_CommandList();

        void Begin(const RHI_Queue* queue);
        void Submit(RHI_Queue* queue, const uint64_t swapchain_id);
        void WaitForExecution();
        void SetPipelineState(RHI_PipelineState& pso);

        // clear
        void ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state);
        void ClearRenderTarget(
            RHI_Texture* texture,
            const Color& clear_color     = rhi_color_load,
            const float clear_depth      = rhi_depth_load,
            const uint32_t clear_stencil = rhi_stencil_load
        );

        // draw
        void Draw(const uint32_t vertex_count, const uint32_t vertex_start_index = 0);
        void DrawIndexed(const uint32_t index_count, const uint32_t index_offset = 0, const uint32_t vertex_offset = 0, const uint32_t instance_start_index = 0, const uint32_t instance_count = 1);

        // dispatch
        void Dispatch(uint32_t x, uint32_t y, uint32_t z = 1);
        void Dispatch(RHI_Texture* texture);

        // blit
        void Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float source_scaling = 1.0f);
        void Blit(RHI_Texture* source, RHI_SwapChain* destination);

        // copy
        void Copy(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips);
        void Copy(RHI_Texture* source, RHI_SwapChain* destination);

        // viewport
        void SetViewport(const RHI_Viewport& viewport) const;
        
        // scissor
        void SetScissorRectangle(const Math::Rectangle& scissor_rectangle) const;

        // cull mode
        void SetCullMode(const RHI_CullMode cull_mode);
        
        // vertex buffer
        void SetBufferVertex(const RHI_VertexBuffer* buffer, const uint32_t binding = 0);
        
        // index buffer
        void SetBufferIndex(const RHI_IndexBuffer* buffer);

        // constant buffer
        void SetConstantBuffer(const uint32_t slot, RHI_ConstantBuffer* constant_buffer) const;
        void SetConstantBuffer(const Renderer_BindingsCb slot, const std::shared_ptr<RHI_ConstantBuffer>& constant_buffer) const { SetConstantBuffer(static_cast<uint32_t>(slot), constant_buffer.get()); }

        // push constant buffer
        void PushConstants(const uint32_t offset, const uint32_t size, const void* data);
        template<typename T>
        void PushConstants(const T& data) { PushConstants(0, sizeof(T), &data); }

        // sampler
        void SetSampler(const uint32_t slot, RHI_Sampler* sampler) const;
        void SetSampler(const uint32_t slot, const std::shared_ptr<RHI_Sampler>& sampler) const { SetSampler(slot, sampler.get()); }

        // texture
        void SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0, const bool uav = false);
        void SetTexture(const Renderer_BindingsUav slot,                       RHI_Texture* texture,  const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0) { SetTexture(static_cast<uint32_t>(slot), texture,       mip_index, mip_range, true); }
        void SetTexture(const Renderer_BindingsUav slot, const std::shared_ptr<RHI_Texture>& texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0) { SetTexture(static_cast<uint32_t>(slot), texture.get(), mip_index, mip_range, true); }
        void SetTexture(const Renderer_BindingsSrv slot,                       RHI_Texture* texture,  const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0) { SetTexture(static_cast<uint32_t>(slot), texture,       mip_index, mip_range, false); }
        void SetTexture(const Renderer_BindingsSrv slot, const std::shared_ptr<RHI_Texture>& texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0) { SetTexture(static_cast<uint32_t>(slot), texture.get(), mip_index, mip_range, false); }

        // structured buffer
        void SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer) const;
        void SetStructuredBuffer(const Renderer_BindingsUav slot, const std::shared_ptr<RHI_StructuredBuffer>& structured_buffer) const { SetStructuredBuffer(static_cast<uint32_t>(slot), structured_buffer.get()); }

        // markers
        void BeginMarker(const char* name);
        void EndMarker();

        // timestamp queries
        uint32_t BeginTimestamp();
        void EndTimestamp();
        float GetTimestampResult(const uint32_t index_timestamp);

        // occlusion queries
        void BeginOcclusionQuery(const uint64_t entity_id);
        void EndOcclusionQuery();
        bool GetOcclusionQueryResult(const uint64_t entity_id);
        void UpdateOcclusionQueries();

        // timeblocks (markers + timestamps)
        void BeginTimeblock(const char* name, const bool gpu_marker = true, const bool gpu_timing = true);
        void EndTimeblock();

        // memory barriers
        void InsertBarrierTexture(void* image, const uint32_t aspect_mask, const uint32_t mip_index, const uint32_t mip_range, const uint32_t array_length, const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new, const bool is_depth);
        void InsertBarrierTexture(RHI_Texture* texture, const uint32_t mip_start, const uint32_t mip_range, const uint32_t array_length, const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new);
        void InsertBarrierTextureReadWrite(RHI_Texture* texture);

        // misc
        void SetIgnoreClearValues(const bool ignore_clear_values) { m_ignore_clear_values = ignore_clear_values; }
        RHI_Semaphore* GetRenderingCompleteSemaphore()            { return m_rendering_complete_semaphore.get(); }
        void* GetRhiResource() const                              { return m_rhi_resource; }
        const RHI_CommandListState GetState() const               { return m_state; }
        uint64_t GetSwapchainId() const                           { return m_swapchain_id; }

    private:
        void PreDraw();
        void RenderPassBegin();
        void RenderPassEnd();

        // sync
        std::shared_ptr<RHI_Semaphore> m_rendering_complete_semaphore;
        std::shared_ptr<RHI_Semaphore> m_rendering_complete_semaphore_timeline;

        // misc
        uint64_t m_buffer_id_vertex                          = 0;
        uint64_t m_buffer_id_index                           = 0;
        bool m_ignore_clear_values                           = false;
        uint64_t m_swapchain_id                              = 0;
        uint32_t m_timestamp_index                           = 0;
        RHI_Pipeline* m_pipeline                             = nullptr;
        bool m_render_pass_active                            = false;
        RHI_DescriptorSetLayout* m_descriptor_layout_current = nullptr;
        std::atomic<RHI_CommandListState> m_state            = RHI_CommandListState::Idle;
        RHI_CullMode m_cull_mode                             = RHI_CullMode::Back;
        const char* m_timeblock_active                       = nullptr;
        static bool m_memory_query_support;
        std::mutex m_mutex_reset;
        RHI_PipelineState m_pso;

        // rhi resources
        void* m_rhi_resource              = nullptr;
        void* m_rhi_cmd_pool_resource     = nullptr;
        void* m_rhi_query_pool_timestamps = nullptr;
        void* m_rhi_query_pool_occlusion  = nullptr;
    };
}
