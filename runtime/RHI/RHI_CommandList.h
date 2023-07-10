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
#include "RHI_Definition.h"
#include "RHI_PipelineState.h"
#include "RHI_Descriptor.h"
#include "../Core/Object.h"
#include "../Rendering/Renderer_Definitions.h"
//============================================

namespace Spartan
{
    // Forward declarations
    namespace Math { class Rectangle; }

    enum class RHI_CommandListState : uint8_t
    {
        Idle,
        Recording,
        Ended,
        Submitted
    };

    class SP_CLASS RHI_CommandList : public Object
    {
    public:
        RHI_CommandList(const RHI_Queue_Type queue_type, const uint32_t index, void* cmd_pool_resource, const char* name);
        ~RHI_CommandList();

        void Begin();
        void End();
        void Submit();
        // Waits for the command list to finish being processed.
        void Wait(const bool log_on_wait = true);

        // Render pass
        void SetPipelineState(RHI_PipelineState& pso);
        void BeginRenderPass();
        void EndRenderPass();

        // Clear
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

        // Draw
        void Draw(uint32_t vertex_count, uint32_t vertex_start_index = 0);
        void DrawIndexed(uint32_t index_count, uint32_t index_offset = 0, uint32_t vertex_offset = 0);

        // Dispatch
        void Dispatch(uint32_t x, uint32_t y, uint32_t z = 1, bool async = false);

        // Blit
        void Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips);

        // Viewport
        void SetViewport(const RHI_Viewport& viewport) const;
        
        // Scissor
        void SetScissorRectangle(const Math::Rectangle& scissor_rectangle) const;
        
        // Vertex buffer
        void SetBufferVertex(const RHI_VertexBuffer* buffer);
        
        // Index buffer
        void SetBufferIndex(const RHI_IndexBuffer* buffer);

        // Constant buffer
        void SetConstantBuffer(const uint32_t slot, const uint8_t scope, RHI_ConstantBuffer* constant_buffer) const;
        inline void SetConstantBuffer(const Renderer_BindingsCb slot, const uint8_t scope, const std::shared_ptr<RHI_ConstantBuffer>& constant_buffer) const { SetConstantBuffer(static_cast<uint32_t>(slot), scope, constant_buffer.get()); }

        // Sampler
        void SetSampler(const uint32_t slot, RHI_Sampler* sampler) const;
        inline void SetSampler(const uint32_t slot, const std::shared_ptr<RHI_Sampler>& sampler) const { SetSampler(slot, sampler.get()); }

        // Texture
        void SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0, const bool uav = false);
        inline void SetTexture(const Renderer_BindingsUav slot,                        RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0) { SetTexture(static_cast<uint32_t>(slot), texture,       mip_index, mip_range, true); }
        inline void SetTexture(const Renderer_BindingsUav slot, const std::shared_ptr<RHI_Texture>& texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0) { SetTexture(static_cast<uint32_t>(slot), texture.get(), mip_index, mip_range, true); }
        inline void SetTexture(const Renderer_BindingsSrv slot,                        RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0) { SetTexture(static_cast<uint32_t>(slot), texture,       mip_index, mip_range, false); }
        inline void SetTexture(const Renderer_BindingsSrv slot, const std::shared_ptr<RHI_Texture>& texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0) { SetTexture(static_cast<uint32_t>(slot), texture.get(), mip_index, mip_range, false); }

        // Structured buffer
        void SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer) const;
        inline void SetStructuredBuffer(const Renderer_BindingsUav slot, const std::shared_ptr<RHI_StructuredBuffer>& structured_buffer) const { SetStructuredBuffer(static_cast<uint32_t>(slot), structured_buffer.get()); }

        // Markers
        void BeginMarker(const char* name);
        void EndMarker();

        // Timestamps
        void BeginTimestamp(void* query);
        void EndTimestamp(void* query);
        float GetTimestampDuration(void* query_start, void* query_end, const uint32_t pass_index);

        // Timeblocks (Markers + Timestamps)
        void BeginTimeblock(const char* name, const bool gpu_marker = true, const bool gpu_timing = true);
        void EndTimeblock();

        // GPU
        static uint32_t GetGpuMemory();
        static uint32_t GetGpuMemoryUsed();

        // State
        const RHI_CommandListState GetState() const { return m_state; }
        bool IsExecuting();

        // Sync
        RHI_Semaphore* GetSemaphoreProccessed() { return m_proccessed_semaphore.get(); }

        // Misc
        void* GetRhiResource() const { return m_rhi_resource; }
        uint32_t GetIndex()    const { return m_index; }

    private:
        void OnDraw();

        // Descriptors
        void GetDescriptorSetLayoutFromPipelineState(RHI_PipelineState& pipeline_state);
        void GetDescriptorsFromPipelineState(RHI_PipelineState& pipeline_state, std::vector<RHI_Descriptor>& descriptors);

        RHI_Pipeline* m_pipeline                         = nullptr;
        bool m_is_rendering                              = false;
        bool m_pipeline_dirty                            = false;
        std::atomic<RHI_CommandListState> m_state        = RHI_CommandListState::Idle;
        static const uint8_t m_resource_array_length_max = 16;
        RHI_SwapChain* swapchain_to_transition           = nullptr;
        static bool m_memory_query_support;
        std::mutex m_mutex_reset;
        uint32_t m_index = 0;

        // Sync
        std::shared_ptr<RHI_Fence> m_proccessed_fence;
        std::shared_ptr<RHI_Semaphore> m_proccessed_semaphore;

        // Descriptors
        std::unordered_map<uint64_t, std::shared_ptr<RHI_DescriptorSetLayout>> m_descriptor_set_layouts;
        RHI_DescriptorSetLayout* m_descriptor_layout_current = nullptr;

        // Pipelines
        RHI_PipelineState m_pso;

        // Keep track of output textures so that we can unbind them and prevent
        // D3D11 warnings when trying to bind them as SRVs in following passes
        struct OutputTexture
        {
            RHI_Texture* texture = nullptr;
            uint32_t slot;
            int mip;
            bool ranged;
        };
        std::array<OutputTexture, m_resource_array_length_max> m_output_textures;
        uint32_t m_output_textures_index = 0;

        // Profiling
        const char* m_timeblock_active         = nullptr;
        void* m_query_pool                     = nullptr;
        uint32_t m_timestamp_index             = 0;
        static const uint32_t m_max_timestamps = 512;
        std::array<uint64_t, m_max_timestamps> m_timestamps;

        // Variables to minimise state changes
        uint64_t m_vertex_buffer_id = 0;
        uint64_t m_index_buffer_id  = 0;

        // RHI Resources
        void* m_rhi_resource          = nullptr;
        void* m_rhi_cmd_pool_resource = nullptr;

        RHI_Queue_Type m_queue_type = RHI_Queue_Type::Undefined;
    };
}
