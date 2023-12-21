/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "../Core/SpObject.h"
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
        Ended,
        Submitted
    };

    class SP_CLASS RHI_CommandList : public SpObject
    {
    public:
        RHI_CommandList(const RHI_Queue_Type queue_type, const uint64_t swapchain_id, void* cmd_pool_resource, const char* name);
        ~RHI_CommandList();

        void Begin();
        void End();
        void Submit();
        void WaitForExecution();
        void SetPipelineState(RHI_PipelineState& pso);

        // clear
        void ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state);
        void ClearRenderTarget(
            RHI_Texture* texture,
            const uint32_t color_index         = 0,
            const uint32_t depth_stencil_index = 0,
            const bool storage                 = false,
            const Color& clear_color           = rhi_color_load,
            const float clear_depth            = rhi_depth_load,
            const uint32_t clear_stencil       = rhi_stencil_load
        );

        // draw
        void Draw(const uint32_t vertex_count, const uint32_t vertex_start_index = 0);
        void DrawIndexed(const uint32_t index_count, const uint32_t index_offset = 0, const uint32_t vertex_offset = 0, const uint32_t instance_start_index = 0, const uint32_t instance_count = 1);

        // dispatch
        void Dispatch(uint32_t x, uint32_t y, uint32_t z = 1, bool async = false);

        // blit
        void Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips);
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

        // timestamps
        uint32_t BeginTimestamp();
        void EndTimestamp();
        float GetTimestampDuration(const uint32_t timestamp_index);

        // timeblocks (Markers + Timestamps)
        void BeginTimeblock(const char* name, const bool gpu_marker = true, const bool gpu_timing = true);
        void EndTimeblock();

        // state
        const RHI_CommandListState GetState() const { return m_state; }
        bool IsExecuting();

        // memory Barriers
        void InsertMemoryBarrierImage(void* image, const uint32_t aspect_mask, const uint32_t mip_index, const uint32_t mip_range, const uint32_t array_length, const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new);
        void InsertMemoryBarrierImage(RHI_Texture* texture, const uint32_t mip_start, const uint32_t mip_range, const uint32_t array_length, const RHI_Image_Layout layout_old, const RHI_Image_Layout layout_new);
        void InsertMemoryBarrierImageWaitForWrite(RHI_Texture* texture);

        // misc
        RHI_Semaphore* GetSemaphoreProccessed() { return m_proccessed_semaphore.get(); }
        void* GetRhiResource() const { return m_rhi_resource; }

    private:
        void OnPreDrawDispatch();
        void BeginRenderPass();
        void EndRenderPass();

        // sync
        std::shared_ptr<RHI_Fence> m_proccessed_fence;
        std::shared_ptr<RHI_Semaphore> m_proccessed_semaphore;

        // profiling
        const char* m_timeblock_active         = nullptr;
        uint32_t m_timestamp_index             = 0;
        static const uint32_t m_max_timestamps = 512;
        std::array<uint64_t, m_max_timestamps> m_timestamps;

        // variables to minimise state changes
        uint64_t m_vertex_buffer_id = 0;
        uint64_t m_index_buffer_id  = 0;

        // misc
        RHI_Pipeline* m_pipeline                             = nullptr;
        bool m_render_pass_active                            = false;
        bool m_pipeline_dirty                                = false;
        static const uint8_t m_resource_array_length_max     = 16;
        RHI_DescriptorSetLayout* m_descriptor_layout_current = nullptr;
        std::atomic<RHI_CommandListState> m_state            = RHI_CommandListState::Idle;
        RHI_Queue_Type m_queue_type                          = RHI_Queue_Type::Undefined;
        RHI_CullMode m_cull_mode                             = RHI_CullMode::Undefined;
        static bool m_memory_query_support;
        std::mutex m_mutex_reset;
        RHI_PipelineState m_pso;

        // rhi resources
        void* m_rhi_resource          = nullptr;
        void* m_rhi_cmd_pool_resource = nullptr;
        void* m_rhi_query_pool        = nullptr;
    };
}
