/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include <atomic>
#include "RHI_Definitions.h"
#include "RHI_PipelineState.h"
#include "../Rendering/Renderer_Definitions.h"
#include <SpartanObject.h>
#include <stack>
//============================================

namespace spartan
{
    // forward declaration
    namespace math { class Rectangle; }

    enum class RHI_CommandListState : uint8_t
    {
        Idle,
        Recording,
        Submitted
    };

    struct ImageBarrierInfo
    {
        void* image                 = nullptr;
        uint32_t aspect_mask        = 0;
        uint32_t mip_index          = 0;
        uint32_t mip_range          = 0;
        uint32_t array_length       = 0;
        RHI_Image_Layout layout_old = RHI_Image_Layout::Max;
        RHI_Image_Layout layout_new = RHI_Image_Layout::Max;
        bool is_depth               = false;
    };

    class RHI_CommandList : public SpartanObject
    {
    public:
        RHI_CommandList(RHI_Queue* queue, void* cmd_pool, const char* name);
        ~RHI_CommandList();

        void Begin();
        void Submit(RHI_SyncPrimitive* semaphore_wait, const bool is_immediate);
        void WaitForExecution(const bool log_wait_time = false);
        void SetPipelineState(RHI_PipelineState& pso);

        // immediate execution
        static RHI_CommandList* ImmediateExecutionBegin(const RHI_Queue_Type queue_type);
        static void ImmediateExecutionEnd(RHI_CommandList* cmd_list);
        static void ImmediateExecutionShutdown();

        // clear
        void ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state);
        void ClearTexture(
            RHI_Texture* texture,
            const Color& clear_color     = rhi_color_load,
            const float clear_depth      = rhi_depth_load,
            const uint32_t clear_stencil = rhi_stencil_load
        );

        // draw
        void Draw(const uint32_t vertex_count, const uint32_t vertex_start_index = 0);
        void DrawIndexed(const uint32_t index_count, const uint32_t index_offset = 0, const uint32_t vertex_offset = 0, const uint32_t instance_index = 0, const uint32_t instance_count = 1);

        // dispatch
        void Dispatch(uint32_t x, uint32_t y, uint32_t z = 1);
        void Dispatch(RHI_Texture* texture, float resolution_scale = 1.0f);

        // trace rays
        void TraceRays(const uint32_t width, const uint32_t height);

        // blit
        void Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float source_scaling = 1.0f);
        void Blit(RHI_Texture* source, RHI_SwapChain* destination);

        // copy
        void Copy(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips);
        void Copy(RHI_Texture* source, RHI_SwapChain* destination);

        // viewport
        void SetViewport(const RHI_Viewport& viewport) const;
        
        // scissor
        void SetScissorRectangle(const math::Rectangle& scissor_rectangle) const;

        // cull mode
        void SetCullMode(const RHI_CullMode cull_mode);
        
        // buffers
        void SetBufferVertex(const RHI_Buffer* vertex, RHI_Buffer* instance = nullptr);
        void SetBufferIndex(const RHI_Buffer* buffer);
        void SetBuffer(const uint32_t slot, RHI_Buffer* buffer) const;
        void SetBuffer(const Renderer_BindingsUav slot, RHI_Buffer* buffer) const { SetBuffer(static_cast<uint32_t>(slot), buffer); }

        // constant buffer
        void SetConstantBuffer(const uint32_t slot, RHI_Buffer* constant_buffer) const;
        void SetConstantBuffer(const Renderer_BindingsCb slot, RHI_Buffer* constant_buffer) const { SetConstantBuffer(static_cast<uint32_t>(slot), constant_buffer); }

        // push constant buffer
        void PushConstants(const uint32_t offset, const uint32_t size, const void* data);
        template<typename T>
        void PushConstants(const T& data) { PushConstants(0, sizeof(T), &data); }

        // texture
        void SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0, const bool uav = false);
        void SetTexture(const Renderer_BindingsUav slot, RHI_Texture* texture,  const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0) { SetTexture(static_cast<uint32_t>(slot), texture, mip_index, mip_range, true); }
        void SetTexture(const Renderer_BindingsSrv slot, RHI_Texture* texture,  const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0) { SetTexture(static_cast<uint32_t>(slot), texture, mip_index, mip_range, false); }

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

        // timeblocks (cpu and gpu time measurement as well as gpu markers)
        void BeginTimeblock(const char* name, const bool gpu_marker = true, const bool gpu_timing = true);
        void EndTimeblock();

        // buffer
        void UpdateBuffer(RHI_Buffer* buffer, const uint64_t offset, const uint64_t size, const void* data);

        // memory barriers
        void InsertBarrier(
            void* image,
            const RHI_Format format,
            const uint32_t mip_index,
            const uint32_t mip_range,
            const uint32_t array_length,
            const RHI_Image_Layout layout_new
        );
        void InsertBarrierReadWrite(RHI_Texture* texture, const RHI_BarrierType type);
        void InsertBarrierReadWrite(RHI_Buffer* buffer);
        void InsertPendingBarrierGroup();

        // layouts
        static void RemoveLayout(void* image);
        static RHI_Image_Layout GetImageLayout(void* image, uint32_t mip_index);

        // misc
        void RenderPassEnd();
        RHI_SyncPrimitive* GetRenderingCompleteSemaphore() { return m_rendering_complete_semaphore.get(); }
        void* GetRhiResource() const                       { return m_rhi_resource; }
        const RHI_CommandListState GetState() const        { return m_state; }
        RHI_Queue* GetQueue() const                        { return m_queue; }
        void CopyTextureToBuffer(RHI_Texture* source, RHI_Buffer* destination);

    private:
        void PreDraw();
        void RenderPassBegin();

        // sync
        std::shared_ptr<RHI_SyncPrimitive> m_rendering_complete_semaphore;
        std::shared_ptr<RHI_SyncPrimitive> m_rendering_complete_semaphore_timeline;

        // misc
        uint64_t m_buffer_id_vertex                          = 0;
        uint64_t m_buffer_id_instance                        = 0;
        uint64_t m_buffer_id_index                           = 0;
        uint32_t m_timestamp_index                           = 0;
        RHI_Pipeline* m_pipeline                             = nullptr;
        RHI_DescriptorSetLayout* m_descriptor_layout_current = nullptr;
        std::atomic<RHI_CommandListState> m_state            = RHI_CommandListState::Idle;
        RHI_CullMode m_cull_mode                             = RHI_CullMode::Back;
        bool m_render_pass_active                            = false;
        std::stack<const char*> m_active_timeblocks;
        std::stack<const char*> m_debug_label_stack;
        std::mutex m_mutex_reset;
        RHI_PipelineState m_pso;
        std::vector<ImageBarrierInfo> m_image_barriers;
        RHI_Queue* m_queue = nullptr;
        bool m_load_depth_render_target = false;
        std::array<bool, rhi_max_render_target_count> m_load_color_render_targets = { false };

        // rhi resources
        void* m_rhi_resource                       = nullptr;
        void* m_rhi_cmd_pool_resource              = nullptr;
        void* m_rhi_query_pool_timestamps          = nullptr;
        void* m_rhi_query_pool_pipeline_statistics = nullptr;
        void* m_rhi_query_pool_occlusion           = nullptr;
    };
}
