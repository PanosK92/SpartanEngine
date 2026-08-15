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
#include "../core/SpartanObject.h"
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
        bool per_mip_layouts_differ                                     = false;
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
        bool IsExecutionComplete();

        // immediate execution
        static RHI_CommandList* ImmediateExecutionBegin(const RHI_Queue_Type queue_type);
        static void ImmediateExecutionEnd(RHI_CommandList* cmd_list);
        static void ImmediateExecutionShutdown();

        RHI_SyncPrimitive* GetTimelineSemaphore()                  { return m_rendering_complete_semaphore_timeline.get(); }
        uint64_t GetLastTimelineSignalValue() const                { return m_last_timeline_signal_value; }
        const RHI_CommandListState GetState() const                { return m_state; }
        RHI_Queue* GetQueue() const                                { return m_queue; }
        void* GetRhiResourcePipeline();
        void* GetRhiResource() const { return m_rhi_resource; }
        void* GetRhiState() const    { return m_rhi_state; }
        uint32_t begin_timestamp();
        uint32_t end_timestamp();
        float GetTimestampResult(const uint32_t index_timestamp);
        float GetTimestampStartMs(const uint32_t index_timestamp);
        void ReadbackTimestampsForProfiler();
        uint64_t GetTimestampRawTick(uint32_t index) const { return (index < m_max_timestamps) ? m_timestamp_data[index] : 0; }
        bool GetOcclusionQueryResult(const uint64_t entity_id);

    private:
        void set_pipeline_state(RHI_PipelineState& pso);
        const RHI_PipelineState& get_pipeline_state() const { return m_pso; }

        // pass, mutates the pending pso and binds when it is complete
        void begin_pass(const char* name);
        void end_pass();
        void set_pass(const char* name);
        void set_shader(RHI_Shader* shader, const char* name = nullptr);
        void set_shaders(RHI_Shader* shader_a, RHI_Shader* shader_b, RHI_Shader* shader_c = nullptr);
        void set_color_target(RHI_Texture* texture);
        void set_color_targets(
            RHI_Texture* t0,
            RHI_Texture* t1 = nullptr,
            RHI_Texture* t2 = nullptr,
            RHI_Texture* t3 = nullptr,
            RHI_Texture* t4 = nullptr,
            RHI_Texture* t5 = nullptr,
            RHI_Texture* t6 = nullptr,
            RHI_Texture* t7 = nullptr
        );
        void set_depth_target(RHI_Texture* texture);
        void set_swap_chain(RHI_SwapChain* swapchain);
        void set_blend_state(RHI_BlendState* state);
        void set_rasterizer_state(RHI_RasterizerState* state);
        void set_depth_stencil_state(RHI_DepthStencilState* state);
        void set_primitive_topology(RHI_PrimitiveTopology topology);
        void set_clear_color(uint32_t index, const Color& color);
        void set_clear_depth(float depth);
        void set_vrs_texture(RHI_Texture* texture);
        void set_resolution_scale(bool enabled);
        void set_multiview(bool enabled);
        void set_array_index(uint32_t index);

        // clear
        void clear_pipeline_state_render_targets(RHI_PipelineState& pipeline_state);
        void clear_texture(
            RHI_Texture* texture,
            const Color& clear_color     = rhi_color_load,
            const float clear_depth      = rhi_depth_load,
            const uint32_t clear_stencil = rhi_stencil_load
        );

        // draw
        void draw(const uint32_t vertex_count, const uint32_t vertex_start_index = 0);
        void draw_indexed(const uint32_t index_count, const uint32_t index_offset = 0, const uint32_t vertex_offset = 0, const uint32_t instance_index = 0, const uint32_t instance_count = 1);
        void draw_indexed_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset, const uint32_t draw_count = 1);
        void draw_indexed_indirect_count(RHI_Buffer* args_buffer, const uint32_t args_offset, RHI_Buffer* count_buffer, const uint32_t count_offset, const uint32_t max_draw_count);
        void draw_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset);
        // one mesh workgroup per IndirectDispatchArgs.group_count_x, args layout matches VkDrawMeshTasksIndirectCommandEXT / D3D12_DISPATCH_MESH_ARGUMENTS
        void draw_mesh_tasks_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset = 0);

        // dispatch
        void dispatch(uint32_t x, uint32_t y, uint32_t z = 1);
        void dispatch(RHI_Texture* texture, float resolution_scale = 1.0f);
        void dispatch_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset = 0);

        // trace rays
        void trace_rays(const uint32_t width, const uint32_t height);

        // blit
        void blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float source_scaling = 1.0f);
        void blit(RHI_Texture* source, RHI_SwapChain* destination);
        void blit_to_array_layer(RHI_Texture* source, RHI_Texture* destination, uint32_t dst_layer);
        void blit_to_xr_swapchain(RHI_Texture* source); // blit to openxr swapchain with aspect ratio preservation
        void prepare_for_present(RHI_SwapChain* swapchain);
        void prepare_texture_for_upload(RHI_Texture* texture);
        void prepare_textures_for_sampling(const std::array<RHI_Texture*, rhi_max_array_size>* textures);
        void prepare_buffer_for_compute(RHI_Buffer* buffer);
        void prepare_buffer_for_readback(RHI_Buffer* buffer);
        // compute writes then mesh/vs reads, renderdoc serializes this so freestanding runs race without it
        void prepare_buffer_for_graphics(RHI_Buffer* buffer);

        // copy
        void copy(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips);
        void copy(RHI_Texture* source, RHI_SwapChain* destination);

        // viewport
        void set_viewport(const RHI_Viewport& viewport) const;
        
        // scissor
        void set_scissor_rectangle(const math::Rectangle& scissor_rectangle) const;

        // cull mode
        void set_cull_mode(const RHI_CullMode cull_mode);
        
        // buffers
        void set_buffer_vertex(const RHI_Buffer* vertex, RHI_Buffer* instance = nullptr);
        void set_buffer_index(const RHI_Buffer* buffer);
        void set_buffer(const uint32_t slot, RHI_Buffer* buffer);
        void set_buffer(const char* name, RHI_Buffer* buffer);

        // constant buffer
        void set_constant_buffer(const uint32_t slot, RHI_Buffer* constant_buffer);
        void set_constant_buffer(const char* name, RHI_Buffer* constant_buffer);

        // push constant buffer
        void push_constants(const uint32_t offset, const uint32_t size, const void* data);
        template<typename T>
        void push_constants(const T& data) { push_constants(0, sizeof(T), &data); }

        // texture
        void set_texture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0, const bool uav = false, const uint32_t array_layer = rhi_all_mips);
        void set_texture(const char* name, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0, const uint32_t array_layer = rhi_all_mips);

        // acceleration structure
        void set_acceleration_structure(const uint32_t slot, RHI_AccelerationStructure* tlas);
        void set_acceleration_structure(const char* name, RHI_AccelerationStructure* tlas);

        // markers
        void begin_marker(const char* name);
        void end_marker();

        // gpu breadcrumbs - writes a uint32 value into a slot of the breadcrumb buffer on the gpu timeline
        void write_gpu_breadcrumb(RHI_Buffer* buffer, uint32_t slot, uint32_t value);

        // occlusion queries
        void begin_occlusion_query(const uint64_t entity_id);
        void end_occlusion_query();
        void update_occlusion_queries();

        // timeblocks (cpu and gpu time measurement as well as gpu markers)
        void begin_timeblock(const char* name, const bool gpu_marker = true, const bool gpu_timing = true);
        void end_timeblock();

        // buffer
        void update_buffer(RHI_Buffer* buffer, const uint64_t offset, const uint64_t size, const void* data, const bool use_mapped_memory = true);

        // misc
        void render_pass_end();
        void restore_after_external_pass();
        void copy_texture_to_buffer(RHI_Texture* source, RHI_Buffer* destination);
        void copy_buffer_to_buffer(void* source, RHI_Buffer* destination, uint64_t size);
        void copy_buffer_to_buffer(RHI_Buffer* source, RHI_Buffer* destination, uint64_t size);

    public:
        // bound list, Class::Method, no instance pointer
        static const RHI_PipelineState& GetPipelineState();
        static void SetPipelineState(RHI_PipelineState& pso);
        static void SetPipelineState(RHI_CommandList* cmd_list, RHI_PipelineState& pso);
        static void BeginPass(const char* name);
        static void EndPass();
        static void SetPass(const char* name);
        static void SetShader(RHI_Shader* shader, const char* name = nullptr);
        static void SetShaders(RHI_Shader* shader_a, RHI_Shader* shader_b, RHI_Shader* shader_c = nullptr);
        static void SetColorTarget(RHI_Texture* texture);
        static void SetColorTargets(
            RHI_Texture* t0,
            RHI_Texture* t1 = nullptr,
            RHI_Texture* t2 = nullptr,
            RHI_Texture* t3 = nullptr,
            RHI_Texture* t4 = nullptr,
            RHI_Texture* t5 = nullptr,
            RHI_Texture* t6 = nullptr,
            RHI_Texture* t7 = nullptr
        );
        static void SetDepthTarget(RHI_Texture* texture);
        static void SetSwapChain(RHI_SwapChain* swapchain);
        static void SetBlendState(RHI_BlendState* state);
        static void SetRasterizerState(RHI_RasterizerState* state);
        static void SetDepthStencilState(RHI_DepthStencilState* state);
        static void SetPrimitiveTopology(RHI_PrimitiveTopology topology);
        static void SetClearColor(uint32_t index, const Color& color);
        static void SetClearDepth(float depth);
        static void SetVrsTexture(RHI_Texture* texture);
        static void SetResolutionScale(bool enabled);
        static void SetMultiview(bool enabled);
        static void SetArrayIndex(uint32_t index);
        static void ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state);
        static void ClearTexture(
            RHI_Texture* texture,
            const Color& clear_color = rhi_color_load,
            const float clear_depth = rhi_depth_load,
            const uint32_t clear_stencil = rhi_stencil_load
        );
        static void Draw(const uint32_t vertex_count, const uint32_t vertex_start_index = 0);
        static void DrawIndexed(const uint32_t index_count, const uint32_t index_offset = 0, const uint32_t vertex_offset = 0, const uint32_t instance_index = 0, const uint32_t instance_count = 1);
        static void DrawIndexedIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset, const uint32_t draw_count = 1);
        static void DrawIndexedIndirectCount(RHI_Buffer* args_buffer, const uint32_t args_offset, RHI_Buffer* count_buffer, const uint32_t count_offset, const uint32_t max_draw_count);
        static void DrawIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset);
        static void DrawMeshTasksIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset = 0);
        static void Dispatch(uint32_t x, uint32_t y, uint32_t z = 1);
        static void Dispatch(RHI_CommandList* cmd_list, uint32_t x, uint32_t y, uint32_t z = 1);
        static void Dispatch(RHI_Texture* texture, float resolution_scale = 1.0f);
        static void DispatchIndirect(RHI_Buffer* args_buffer, const uint32_t args_offset = 0);
        static void TraceRays(const uint32_t width, const uint32_t height);
        static void Blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float source_scaling = 1.0f);
        static void Blit(RHI_Texture* source, RHI_SwapChain* destination);
        static void BlitToArrayLayer(RHI_Texture* source, RHI_Texture* destination, uint32_t dst_layer);
        static void BlitToXrSwapchain(RHI_Texture* source);
        static void PrepareForPresent(RHI_SwapChain* swapchain);
        static void PrepareTextureForUpload(RHI_Texture* texture);
        static void PrepareTextureForUpload(RHI_CommandList* cmd_list, RHI_Texture* texture);
        static void PrepareTexturesForSampling(const std::array<RHI_Texture*, rhi_max_array_size>* textures);
        static void PrepareBufferForCompute(RHI_Buffer* buffer);
        static void PrepareBufferForCompute(RHI_CommandList* cmd_list, RHI_Buffer* buffer);
        static void PrepareBufferForReadback(RHI_Buffer* buffer);
        static void PrepareBufferForReadback(RHI_CommandList* cmd_list, RHI_Buffer* buffer);
        static void PrepareBufferForGraphics(RHI_Buffer* buffer);
        static void Copy(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips);
        static void Copy(RHI_Texture* source, RHI_SwapChain* destination);
        static void SetViewport(const RHI_Viewport& viewport);
        static void SetScissorRectangle(const math::Rectangle& scissor_rectangle);
        static void SetCullMode(const RHI_CullMode cull_mode);
        static void SetBufferVertex(const RHI_Buffer* vertex, RHI_Buffer* instance = nullptr);
        static void SetBufferIndex(const RHI_Buffer* buffer);
        static void SetBuffer(const uint32_t slot, RHI_Buffer* buffer);
        static void SetBuffer(RHI_CommandList* cmd_list, const uint32_t slot, RHI_Buffer* buffer);
        static void SetBuffer(const char* name, RHI_Buffer* buffer);
        static void SetConstantBuffer(const uint32_t slot, RHI_Buffer* constant_buffer);
        static void SetConstantBuffer(const char* name, RHI_Buffer* constant_buffer);
        static void PushConstants(const uint32_t offset, const uint32_t size, const void* data);
        static void PushConstants(RHI_CommandList* cmd_list, const uint32_t offset, const uint32_t size, const void* data);
        template<typename T>
        static void PushConstants(const T& data)
        {
            PushConstants(0, sizeof(T), &data);
        }
        template<typename T>
        static void PushConstants(RHI_CommandList* cmd_list, const T& data)
        {
            PushConstants(cmd_list, 0, sizeof(T), &data);
        }
        static void SetTexture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0, const bool uav = false, const uint32_t array_layer = rhi_all_mips);
        static void SetTexture(const char* name, RHI_Texture* texture, const uint32_t mip_index = rhi_all_mips, uint32_t mip_range = 0, const uint32_t array_layer = rhi_all_mips);
        static void SetAccelerationStructure(const uint32_t slot, RHI_AccelerationStructure* tlas);
        static void SetAccelerationStructure(const char* name, RHI_AccelerationStructure* tlas);
        static void BeginMarker(const char* name);
        static void EndMarker();
        static void WriteGpuBreadcrumb(RHI_Buffer* buffer, uint32_t slot, uint32_t value);
        static uint32_t BeginTimestamp();
        static uint32_t EndTimestamp();
        static void BeginOcclusionQuery(const uint64_t entity_id);
        static void EndOcclusionQuery();
        static void UpdateOcclusionQueries();
        static void BeginTimeblock(const char* name, const bool gpu_marker = true, const bool gpu_timing = true);
        static void EndTimeblock();
        static void UpdateBuffer(RHI_Buffer* buffer, const uint64_t offset, const uint64_t size, const void* data, const bool use_mapped_memory = true);
        static void RenderPassEnd();
        static void RestoreAfterExternalPass();
        static void CopyTextureToBuffer(RHI_Texture* source, RHI_Buffer* destination);
        static void CopyBufferToBuffer(void* source, RHI_Buffer* destination, uint64_t size);
        static void CopyBufferToBuffer(RHI_Buffer* source, RHI_Buffer* destination, uint64_t size);
        static void CopyBufferToBuffer(RHI_CommandList* cmd_list, RHI_Buffer* source, RHI_Buffer* destination, uint64_t size);

    private:
        friend class RHI_Texture;
        friend class RHI_VendorTechnology;
        friend class RHI_Device;
        friend class RHI_Buffer;
        friend class RHI_AccelerationStructure;

        // include_pixel_stage widens the d3d12 state to pixel and non pixel, nri barriers on a direct list expect both
        void EnsureComputeShaderResource(RHI_Texture* texture, bool include_pixel_stage = false);
        void AdoptComputeShaderResource(RHI_Texture* texture, bool include_pixel_stage = false);
        void AdoptUnorderedAccess(RHI_Texture* texture);
        void PrepareTextureForSampling(RHI_Texture* texture);
        static void RemoveLayout(void* image);
        static RHI_Image_Layout GetImageLayout(void* image, uint32_t mip_index);
        void InsertBarrier(const RHI_Barrier& barrier);
        void FlushBarriers();
        void InsertBarrier(RHI_Texture* texture, RHI_Image_Layout layout, uint32_t mip = rhi_all_mips, uint32_t mip_range = 0);
        void InsertBarrier(RHI_Buffer* buffer);
        void InsertBarrier(void* image, RHI_Format format, uint32_t mip_index, uint32_t mip_range, uint32_t array_length, RHI_Image_Layout layout);
        void PreDraw();
        void PrepareDispatch();
        void TryBindPendingPipeline();
        bool IsPendingPipelineReady() const;
        void RenderPassBegin();
        void TrackTextureUsage(uint32_t slot, RHI_Texture* texture, uint32_t mip_index, uint32_t mip_range, uint32_t array_layer, bool uav);
        void TrackBufferUsage(uint32_t slot, RHI_Buffer* buffer, RHI_Resource_Access access);
        void TrackBufferRead(uint32_t slot, RHI_Buffer* buffer, RHI_Resource_Usage usage);
        void TrackExternalTextureUsage(RHI_Texture* texture, RHI_Resource_Access access, RHI_Image_Layout layout, RHI_Barrier_Scope scope, RHI_Resource_Usage usage = RHI_Resource_Usage::Shader);
        void PrepareForExternalWrite(RHI_Texture* texture, RHI_Image_Layout layout = RHI_Image_Layout::General, RHI_Barrier_Scope scope = RHI_Barrier_Scope::Compute);
        void SynchronizeRenderTargets();
        void SynchronizeResources(bool include_bindings = true);
        void ValidateBindings();
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
        bool m_occlusion_query_pool_reset                    = false;

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
        // compute cull writes then mesh draws read, one barrier per dirty window is enough
        bool m_mesh_cull_barrier_satisfied                   = false;
        std::stack<const char*> m_active_timeblocks;
        std::stack<const char*> m_debug_label_stack;
        std::stack<int32_t> m_breadcrumb_gpu_slots;
        bool m_bind_dynamic = false;
        void* m_bindless_pipeline_layout = nullptr;
        uint8_t m_bindless_pipeline_type = static_cast<uint8_t>(-1);
        void* m_dynamic_descriptor_set = nullptr;
        void* m_dynamic_pipeline_layout = nullptr;
        uint8_t m_dynamic_pipeline_type = static_cast<uint8_t>(-1);
        std::array<uint32_t, 10> m_dynamic_offsets = {};
        uint32_t m_dynamic_offset_count = 0;
        bool m_pipeline_state_dirty = false;
        bool m_resources_dirty = true;
        bool m_resources_have_write_bindings = false;
        uint32_t m_push_constant_size = 0;
        mutable uint64_t m_texture_bindings_hash = 0;
        mutable uint64_t m_texture_bindings_srv = 0;
        mutable uint64_t m_texture_bindings_uav = 0;
        bool m_batch_barrier_flush = false;
        bool m_flushing_barriers = false;
        RHI_PipelineState m_pso;
        RHI_PipelineState m_pso_pending;
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
        void* m_rhi_state                          = nullptr;
        void* m_rhi_cmd_pool_resource              = nullptr;
        void* m_rhi_query_pool_timestamps          = nullptr;
        void* m_rhi_query_pool_pipeline_statistics = nullptr;
        void* m_rhi_query_pool_occlusion           = nullptr;
        void* m_rhi_fence                          = nullptr;
        void* m_rhi_fence_event                    = nullptr;
        uint64_t m_rhi_fence_value                 = 0;
    };
}
