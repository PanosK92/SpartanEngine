/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include <unordered_map>
#include "RHI_Definitions.h"
#include "RHI_PipelineState.h"
#include "RHI_Buffer.h"
#include "RHI_SyncPrimitive.h"
#include "../Rendering/Renderer_Definitions.h"
#include "../Core/SpartanObject.h"
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

    struct PendingBarrierInfo
    {
        RHI_Barrier barrier;
        void* image                 = nullptr;
        uint32_t aspect_mask        = 0;
        uint32_t mip_index          = 0;
        uint32_t mip_range          = 0;
        uint32_t array_length       = 0;
        RHI_Image_Layout layout_old = RHI_Image_Layout::Max;
        RHI_Image_Layout layout_new = RHI_Image_Layout::Max;
        bool is_depth               = false;

        // for image sync with per-mip views (pre-captured layouts at insert time)
        std::array<RHI_Image_Layout, rhi_max_mip_count> per_mip_layouts = {};
        uint32_t per_mip_count                                          = 0;
        bool has_per_mip_views                                          = false;
    };

    struct RHI_Tracked_Usage
    {
        RHI_Resource_Access access = RHI_Resource_Access::None;
        RHI_Resource_Usage usage   = RHI_Resource_Usage::None;
        RHI_Barrier_Scope scope    = RHI_Barrier_Scope::All;
        RHI_Queue_Type queue       = RHI_Queue_Type::Max;
        RHI_Image_Layout layout    = RHI_Image_Layout::Max;
    };

    struct RHI_Tracked_Texture_Binding
    {
        RHI_Texture* texture       = nullptr;
        uint32_t mip_index         = 0;
        uint32_t mip_range         = 0;
        uint32_t array_layer       = rhi_all_mips;
        RHI_Resource_Access access = RHI_Resource_Access::None;
        RHI_Resource_Usage usage   = RHI_Resource_Usage::Shader;
        RHI_Image_Layout layout    = RHI_Image_Layout::Max;
    };

    struct RHI_Tracked_Buffer_Binding
    {
        RHI_Buffer* buffer         = nullptr;
        RHI_Resource_Access access = RHI_Resource_Access::None;
        RHI_Resource_Usage usage   = RHI_Resource_Usage::None;
    };

    class RHI_CommandList : public SpartanObject
    {
    public:
        RHI_CommandList(RHI_Queue* queue, void* cmd_pool, const char* name);
        ~RHI_CommandList();

        void Begin();
        void Submit(RHI_SyncPrimitive* semaphore_wait, const bool is_immediate, RHI_SyncPrimitive* semaphore_signal = nullptr,
                    RHI_SyncPrimitive* semaphore_timeline_wait = nullptr, uint64_t timeline_wait_value = 0);
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
        void DrawIndexedIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset);
        void DrawIndexedIndirectCount(RHI_Buffer* args_buffer, const uint32_t args_offset, RHI_Buffer* count_buffer, const uint32_t count_offset, const uint32_t max_draw_count);
        void DrawIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset);

        // dispatch
        void Dispatch(uint32_t x, uint32_t y, uint32_t z = 1);
        void Dispatch(RHI_Texture* texture, float resolution_scale = 1.0f);
        void DispatchIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset = 0);

        // trace rays
        void TraceRays(const uint32_t width, const uint32_t height);

        // blit
        void Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float source_scaling = 1.0f);
        void Blit(RHI_Texture* source, RHI_SwapChain* destination);
        void BlitToArrayLayer(RHI_Texture* source, RHI_Texture* destination, uint32_t dst_layer);
        void BlitToXrSwapchain(RHI_Texture* source); // blit to openxr swapchain with aspect ratio preservation
        void PrepareForPresent(RHI_SwapChain* swapchain);
        void PrepareTextureForUpload(RHI_Texture* texture);
        void PrepareTexturesForSampling(const std::array<RHI_Texture*, rhi_max_array_size>* textures);
        void PrepareBufferForCompute(RHI_Buffer* buffer);
        void PrepareBufferForReadback(RHI_Buffer* buffer);

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
        void SetBuffer(const uint32_t slot, RHI_Buffer* buffer);
        void SetBuffer(const Renderer_BindingsUav slot, RHI_Buffer* buffer) { SetBuffer(static_cast<uint32_t>(slot), buffer); }

        // constant buffer
        void SetConstantBuffer(const uint32_t slot, RHI_Buffer* constant_buffer);
        void SetConstantBuffer(const Renderer_BindingsCb slot, RHI_Buffer* constant_buffer) { SetConstantBuffer(static_cast<uint32_t>(slot), constant_buffer); }

        // push constant buffer
        void PushConstants(const uint32_t offset, const uint32_t size, const void* data);
        template<typename T>
        void PushConstants(const T& data) { PushConstants(0, sizeof(T), &data); }

        // texture
        void SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0, const bool uav = false, const uint32_t array_layer = rhi_all_mips);
        void SetTexture(const Renderer_BindingsUav slot, RHI_Texture* texture,  const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0) { SetTexture(static_cast<uint32_t>(slot), texture, mip_index, mip_range, true); }
        void SetTexture(const Renderer_BindingsSrv slot, RHI_Texture* texture,  const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0, const uint32_t array_layer = rhi_all_mips) { SetTexture(static_cast<uint32_t>(slot), texture, mip_index, mip_range, false, array_layer); }

        // acceleration structure
        void SetAccelerationStructure(Renderer_BindingsSrv slot, RHI_AccelerationStructure* tlas);

        // markers
        void BeginMarker(const char* name);
        void EndMarker();

        // gpu breadcrumbs - writes a uint32 value into a slot of the breadcrumb buffer on the gpu timeline
        void WriteGpuBreadcrumb(RHI_Buffer* buffer, uint32_t slot, uint32_t value);

        // timestamp queries
        uint32_t BeginTimestamp();
        uint32_t EndTimestamp();
        float GetTimestampResult(const uint32_t index_timestamp);
        float GetTimestampStartMs(const uint32_t index_timestamp);

        // deferred profiler readback (reads fresh timestamps after gpu execution)
        void ReadbackTimestampsForProfiler();
        uint64_t GetTimestampRawTick(uint32_t index) const { return (index < m_max_timestamps) ? m_timestamp_data[index] : 0; }

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

        // misc
        void RenderPassEnd();
        RHI_SyncPrimitive* GetTimelineSemaphore()                  { return m_rendering_complete_semaphore_timeline.get(); }
        uint64_t GetLastTimelineSignalValue() const                { return m_last_timeline_signal_value; }
        const RHI_CommandListState GetState() const                { return m_state; }
        RHI_Queue* GetQueue() const                                { return m_queue; }
        void CopyTextureToBuffer(RHI_Texture* source, RHI_Buffer* destination);
        void CopyBufferToBuffer(void* source, RHI_Buffer* destination, uint64_t size);
        void CopyBufferToBuffer(RHI_Buffer* source, RHI_Buffer* destination, uint64_t size);

        // rhi
        void* GetRhiResourcePipeline();
        void* GetRhiResource() const { return m_rhi_resource; }

    private:
        friend class RHI_Texture;
        friend class RHI_VendorTechnology;
        friend class RHI_Device;

        void RestoreAfterExternalPass();
        void EnsureComputeShaderResource(RHI_Texture* texture);
        void AdoptComputeShaderResource(RHI_Texture* texture);
        void AdoptUnorderedAccess(RHI_Texture* texture);
        void PrepareTextureForSampling(RHI_Texture* texture);
        static void RemoveLayout(void* image);
        static RHI_Image_Layout GetImageLayout(void* image, uint32_t mip_index);
        void InsertBarrier(const RHI_Barrier& barrier);
        void FlushBarriers();
        void InsertBarrier(RHI_Texture* texture, RHI_Image_Layout layout, uint32_t mip = rhi_all_mips, uint32_t mip_range = 0);
        void InsertBarrier(RHI_Texture* texture, RHI_BarrierType sync_type);
        void InsertBarrier(RHI_Buffer* buffer);
        void InsertBarrier(void* image, RHI_Format format, uint32_t mip_index, uint32_t mip_range, uint32_t array_length, RHI_Image_Layout layout);
        void PreDraw();
        void RenderPassBegin();
        void TrackTextureUsage(uint32_t slot, RHI_Texture* texture, uint32_t mip_index, uint32_t mip_range, uint32_t array_layer, bool uav);
        void TrackBufferUsage(uint32_t slot, RHI_Buffer* buffer, RHI_Resource_Access access);
        void TrackBufferRead(uint32_t slot, RHI_Buffer* buffer, RHI_Resource_Usage usage);
        void TrackExternalTextureUsage(RHI_Texture* texture, RHI_Resource_Access access, RHI_Image_Layout layout, RHI_Barrier_Scope scope, RHI_Resource_Usage usage = RHI_Resource_Usage::Shader);
        void PrepareForExternalWrite(RHI_Texture* texture, RHI_Image_Layout layout = RHI_Image_Layout::General, RHI_Barrier_Scope scope = RHI_Barrier_Scope::Compute);
        void SynchronizeRenderTargets();
        void SynchronizeResources(bool include_bindings = true);
        void ResetTrackedBindings();
        void ResetTrackedResources();
        void CommitTrackedResources();
        RHI_Image_Layout GetTrackedTextureLayout(RHI_Texture* texture, uint32_t mip_index);
        void SetTrackedTextureLayout(RHI_Texture* texture, uint32_t mip_index, uint32_t mip_range, RHI_Image_Layout layout);
        RHI_Image_Layout GetTrackedImageLayout(void* image, uint32_t mip_index);
        void SetTrackedImageLayout(void* image, uint32_t mip_index, uint32_t mip_range, RHI_Image_Layout layout);
        bool IsTextureBindingUsed(uint32_t slot, bool storage) const;
        RHI_Resource_Access GetBufferAccess(uint32_t slot) const;
        RHI_Barrier_Scope GetResourceScope() const;

        // sync
        std::shared_ptr<RHI_SyncPrimitive> m_rendering_complete_semaphore_timeline;
        uint64_t m_last_timeline_signal_value = 0;

        // misc
        uint64_t m_buffer_id_vertex                          = 0;
        uint64_t m_buffer_id_instance                        = 0;
        uint64_t m_buffer_id_index                           = 0;
        uint32_t m_timestamp_index                           = 0;

        // per-command-list timestamp storage (avoids cross-queue data corruption)
        static constexpr uint32_t m_max_timestamps           = 256;
        std::array<uint64_t, m_max_timestamps> m_timestamp_data = {};
        uint64_t m_gpu_frame_reference_tick                  = 0;
        RHI_Pipeline* m_pipeline                             = nullptr;
        RHI_DescriptorSetLayout* m_descriptor_layout_current = nullptr;
        std::unordered_map<uint64_t, std::unique_ptr<RHI_DescriptorSetLayout>> m_descriptor_layouts_local;
        std::atomic<RHI_CommandListState> m_state            = RHI_CommandListState::Idle;
        RHI_CullMode m_cull_mode                             = RHI_CullMode::Back;
        mutable float m_scissor_x                            = 0.0f;
        mutable float m_scissor_y                            = 0.0f;
        mutable float m_scissor_width                        = 0.0f;
        mutable float m_scissor_height                       = 0.0f;
        mutable bool m_scissor_valid                         = false;
        mutable float m_viewport_x                           = 0.0f;
        mutable float m_viewport_y                           = 0.0f;
        mutable float m_viewport_width                       = 0.0f;
        mutable float m_viewport_height                      = 0.0f;
        mutable float m_viewport_depth_min                   = 0.0f;
        mutable float m_viewport_depth_max                   = 0.0f;
        mutable bool m_viewport_valid                        = false;
        mutable bool m_vrs_enabled                           = false;
        mutable bool m_vrs_valid                             = false;
        bool m_render_pass_active                            = false;
        bool m_render_pass_pending                           = false;
        std::stack<const char*> m_active_timeblocks;
        std::stack<const char*> m_debug_label_stack;
        std::stack<int32_t> m_breadcrumb_gpu_slots;
        bool m_bind_dynamic = false;
        bool m_batch_barrier_flush = false;
        bool m_flushing_barriers = false;
        RHI_PipelineState m_pso;
        std::vector<PendingBarrierInfo> m_pending_barriers;
        RHI_Queue* m_queue = nullptr;
        bool m_load_depth_render_target = false;
        std::array<bool, rhi_max_render_target_count> m_load_color_render_targets = { false };
        static constexpr uint32_t m_max_tracked_resource_slots = 64;
        std::array<RHI_Tracked_Texture_Binding, m_max_tracked_resource_slots> m_tracked_textures_srv;
        std::array<RHI_Tracked_Texture_Binding, m_max_tracked_resource_slots> m_tracked_textures_uav;
        std::array<RHI_Tracked_Texture_Binding, rhi_max_render_target_count + 2> m_tracked_attachments;
        std::array<RHI_Tracked_Buffer_Binding, m_max_tracked_resource_slots> m_tracked_buffers;
        std::array<RHI_Tracked_Buffer_Binding, 5> m_tracked_buffers_read;
        std::unordered_map<uint64_t, std::array<RHI_Tracked_Usage, rhi_max_mip_count>> m_tracked_texture_history;
        std::unordered_map<uint64_t, RHI_Tracked_Usage> m_tracked_buffer_history;
        std::unordered_map<RHI_Texture*, std::array<RHI_Tracked_Usage, rhi_max_mip_count>> m_current_texture_usage;
        std::unordered_map<RHI_Buffer*, RHI_Tracked_Usage> m_current_buffer_usage;
        std::unordered_map<RHI_Texture*, std::array<RHI_Image_Layout, rhi_max_mip_count>> m_tracked_texture_layouts;
        std::unordered_map<void*, std::array<RHI_Image_Layout, rhi_max_mip_count>> m_tracked_image_layouts;

        // one sbt per pipeline (keyed by pipeline handle) so it's created once and reused
        std::unordered_map<void*, std::unique_ptr<RHI_Buffer>> m_shader_binding_tables;

        // rhi resources
        void* m_rhi_resource                       = nullptr;
        void* m_rhi_cmd_pool_resource              = nullptr;
        void* m_rhi_query_pool_timestamps          = nullptr;
        void* m_rhi_query_pool_pipeline_statistics = nullptr;
        void* m_rhi_query_pool_occlusion           = nullptr;
        void* m_rhi_fence                          = nullptr;
        void* m_rhi_fence_event                    = nullptr;
        uint64_t m_rhi_fence_value                 = 0;
    };
}
