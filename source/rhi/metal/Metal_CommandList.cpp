/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============================
#include "pch.h"
#include "../RHI_CommandList.h"
#include "../RHI_Device.h"
#include "../RHI_Queue.h"
#include "../RHI_Pipeline.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_SwapChain.h"
#include "../RHI_SyncPrimitive.h"
#include "../RHI_Texture.h"
#include "../RHI_Buffer.h"
#include "../RHI_AccelerationStructure.h"
#include "../../profiling/Profiler.h"
#include "../../error_handling/Breadcrumbs.h"
#include "Metal_Internal.h"
#define IR_RUNTIME_METALCPP
#include <metal_irconverter_runtime/metal_irconverter_runtime.h>
#include <CoreFoundation/CoreFoundation.h>
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        metal_encoder::State& get_state(void* rhi_state)
        {
            return *static_cast<metal_encoder::State*>(rhi_state);
        }

        // the stages a barrier waits on and holds back, producers are any stage, consumers the stages of the encoder
        const MTL::Stages stages_render  = MTL::StageVertex | MTL::StageObject | MTL::StageMesh | MTL::StageFragment;
        const MTL::Stages stages_compute = MTL::StageDispatch | MTL::StageBlit | MTL::StageAccelerationStructure;
        const MTL::Stages stages_all     = stages_render | stages_compute;
    }

    // metal allows only a few timestamp heaps of at most 4096 entries, so lists take ranges of shared heaps
    namespace timestamps
    {
        const uint32_t heap_capacity = 4096;

        vector<MTL4::CounterHeap*> heaps;
        uint32_t next_slot = heap_capacity;
        mutex mutex_heaps;

        // retained for the caller, which releases it when it no longer writes into it
        MTL4::CounterHeap* allocate(const uint32_t slot_count, uint32_t& slot_base)
        {
            lock_guard<mutex> lock(mutex_heaps);
            if (next_slot + slot_count > heap_capacity)
            {
                MTL4::CounterHeapDescriptor* descriptor = MTL4::CounterHeapDescriptor::alloc()->init();
                descriptor->setType(MTL4::CounterHeapTypeTimestamp);
                descriptor->setCount(heap_capacity);
                NS::Error* error        = nullptr;
                MTL4::CounterHeap* heap = RHI_Context::device->newCounterHeap(descriptor, &error);
                descriptor->release();
                if (!heap)
                    return nullptr;

                heaps.push_back(heap);
                next_slot = 0;
            }

            slot_base  = next_slot;
            next_slot += slot_count;
            return heaps.back()->retain();
        }

        void destroy()
        {
            lock_guard<mutex> lock(mutex_heaps);
            for (MTL4::CounterHeap* heap : heaps)
            {
                heap->release();
            }
            heaps.clear();
            next_slot = heap_capacity;
        }
    }

    namespace metal_blit
    {
        // a fullscreen triangle that samples the source, copies can't scale or change format
        const char* source = R"(
            #include <metal_stdlib>
            using namespace metal;

            struct vertex_output
            {
                float4 position [[position]];
                float2 uv;
            };

            vertex vertex_output blit_vs(uint id [[vertex_id]])
            {
                vertex_output output;
                output.uv       = float2((id << 1) & 2, id & 2);
                output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
                return output;
            }

            fragment float4 blit_ps(vertex_output input [[stage_in]], texture2d<float> source [[texture(0)]])
            {
                constexpr sampler sampler_linear(filter::linear, address::clamp_to_edge);
                return source.sample(sampler_linear, input.uv);
            }

            // depth can't be filtered, the nearest sample is written as the fragment's depth
            struct depth_output
            {
                float depth [[depth(any)]];
            };

            fragment depth_output blit_depth_ps(vertex_output input [[stage_in]], depth2d<float> source [[texture(0)]])
            {
                constexpr sampler sampler_point(filter::nearest, address::clamp_to_edge);
                depth_output output;
                output.depth = source.sample(sampler_point, input.uv);
                return output;
            }
        )";

        MTL::Library* library = nullptr;
        unordered_map<MTL::PixelFormat, MTL::RenderPipelineState*> pipelines;
        MTL::DepthStencilState* depth_write = nullptr; // always passes and writes, the blit replaces the destination
        mutex mutex_pipelines;

        bool is_depth(const MTL::PixelFormat format)
        {
            return format == MTL::PixelFormatDepth16Unorm || format == MTL::PixelFormatDepth32Float || format == MTL::PixelFormatDepth32Float_Stencil8;
        }

        MTL::DepthStencilState* get_depth_write()
        {
            lock_guard<mutex> lock(mutex_pipelines);
            if (!depth_write)
            {
                MTL::DepthStencilDescriptor* descriptor = MTL::DepthStencilDescriptor::alloc()->init();
                descriptor->setDepthCompareFunction(MTL::CompareFunctionAlways);
                descriptor->setDepthWriteEnabled(true);
                depth_write = RHI_Context::device->newDepthStencilState(descriptor);
                descriptor->release();
            }
            return depth_write;
        }

        MTL::RenderPipelineState* get_pipeline(const MTL::PixelFormat format)
        {
            lock_guard<mutex> lock(mutex_pipelines);

            auto it = pipelines.find(format);
            if (it != pipelines.end())
                return it->second;

            NS::Error* error = nullptr;
            if (!library)
            {
                NS::String* code = metal_utility::ns_string(source);
                library          = RHI_Context::device->newLibrary(code, nullptr, &error);
                code->release();
                SP_ASSERT_MSG(library != nullptr, "Failed to compile the blit shaders");
            }

            const bool depth                          = is_depth(format);
            NS::String* name_vs                       = metal_utility::ns_string("blit_vs");
            NS::String* name_ps                       = metal_utility::ns_string(depth ? "blit_depth_ps" : "blit_ps");
            MTL::Function* function_vs                = library->newFunction(name_vs);
            MTL::Function* function_ps                = library->newFunction(name_ps);
            MTL::RenderPipelineDescriptor* descriptor = MTL::RenderPipelineDescriptor::alloc()->init();
            descriptor->setVertexFunction(function_vs);
            descriptor->setFragmentFunction(function_ps);
            if (depth)
            {
                descriptor->setDepthAttachmentPixelFormat(format);
                if (format == MTL::PixelFormatDepth32Float_Stencil8)
                {
                    descriptor->setStencilAttachmentPixelFormat(format);
                }
            }
            else
            {
                descriptor->colorAttachments()->object(0)->setPixelFormat(format);
            }
            metal_utility::set_label(descriptor, "blit");

            MTL::RenderPipelineState* pipeline = RHI_Context::device->newRenderPipelineState(descriptor, &error);
            SP_ASSERT_MSG(pipeline != nullptr, "Failed to create the blit pipeline");

            descriptor->release();
            function_vs->release();
            function_ps->release();
            name_vs->release();
            name_ps->release();

            pipelines[format] = pipeline;
            return pipeline;
        }

        void destroy()
        {
            lock_guard<mutex> lock(mutex_pipelines);
            for (auto& [format, pipeline] : pipelines)
            {
                pipeline->release();
            }
            pipelines.clear();

            if (depth_write)
            {
                depth_write->release();
                depth_write = nullptr;
            }

            if (library)
            {
                library->release();
                library = nullptr;
            }
        }
    }

    MTL4::ComputeCommandEncoder* metal_encoder::compute(void* cmd_buffer, State& state, const bool transfer)
    {
        if (state.type != Type::Compute)
        {
            end(state);
            MTL4::ComputeCommandEncoder* encoder = static_cast<MTL4::CommandBuffer*>(cmd_buffer)->computeCommandEncoder();
            encoder->setArgumentTable(state.argument_table);
            metal_utility::set_label(encoder, state.pass_name.c_str());
            state.type    = Type::Compute;
            state.encoder = encoder;
        }

        if (transfer || state.barrier_pending || state.first_encoder)
        {
            barrier(state);
        }
        state.barrier_pending = transfer;

        return static_cast<MTL4::ComputeCommandEncoder*>(state.encoder);
    }

    MTL4::RenderCommandEncoder* metal_encoder::render(void* cmd_buffer, State& state, MTL4::RenderPassDescriptor* descriptor, const bool transfer)
    {
        end(state);
        MTL4::RenderCommandEncoder* encoder = static_cast<MTL4::CommandBuffer*>(cmd_buffer)->renderCommandEncoder(descriptor);
        encoder->setArgumentTable(state.argument_table, MTL::RenderStageVertex | MTL::RenderStageObject | MTL::RenderStageMesh | MTL::RenderStageFragment);
        metal_utility::set_label(encoder, state.pass_name.c_str());
        state.type    = Type::Render;
        state.encoder = encoder;

        if (transfer || state.barrier_pending || state.first_encoder)
        {
            barrier(state);
        }
        state.barrier_pending = transfer;

        return encoder;
    }

    // the queue barrier orders earlier encoders and command buffers, the encoder barrier earlier work in this encoder,
    // inside a render pass only the geometry stages of earlier draws can be waited on
    void metal_encoder::barrier(State& state)
    {
        const bool render                 = state.type == Type::Render;
        const MTL::Stages stages          = render ? stages_render : stages_compute;
        const MTL::Stages stages_previous = render ? MTL::StageVertex | MTL::StageObject | MTL::StageMesh : stages_compute;
        state.encoder->barrierAfterQueueStages(stages_all, stages, MTL4::VisibilityOptionDevice);
        state.encoder->barrierAfterEncoderStages(stages_previous, stages, MTL4::VisibilityOptionDevice);
        state.barrier_pending = false;
        state.first_encoder   = false;
    }

    void metal_encoder::end(State& state)
    {
        if (state.encoder)
        {
            state.encoder->endEncoding();
            state.encoder = nullptr;
        }
        state.type           = Type::None;
        state.pipeline_bound = nullptr;
    }

    namespace
    {
        struct Allocation
        {
            uint8_t* data    = nullptr;
            uint64_t address = 0;
        };

        // linear allocation from the list's upload buffers, a new buffer joins when the current one is full
        Allocation upload(metal_encoder::State& state, const uint64_t size)
        {
            const uint64_t buffer_size  = 4 * 1024 * 1024;
            const uint64_t size_aligned = (size + 15) & ~15ull;

            while (true)
            {
                if (state.upload_index < state.upload_buffers.size())
                {
                    MTL::Buffer* buffer = state.upload_buffers[state.upload_index];
                    if (state.upload_offset + size_aligned <= buffer->length())
                    {
                        Allocation allocation = { static_cast<uint8_t*>(buffer->contents()) + state.upload_offset, buffer->gpuAddress() + state.upload_offset };
                        state.upload_offset  += size_aligned;
                        return allocation;
                    }

                    state.upload_index++;
                    state.upload_offset = 0;
                    continue;
                }

                MTL::Buffer* buffer = RHI_Context::device->newBuffer(max(buffer_size, size_aligned), MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined | MTL::ResourceHazardTrackingModeUntracked);
                SP_ASSERT_MSG(buffer != nullptr, "Failed to create an upload buffer");
                metal_utility::set_label(buffer, "upload");
                metal_residency::Add(metal_residency::Group::Static, buffer);
                state.upload_buffers.push_back(buffer);
            }
        }

        uint64_t upload(metal_encoder::State& state, const void* data, const uint64_t size)
        {
            Allocation allocation = upload(state, size);
            memcpy(allocation.data, data, size);
            return allocation.address;
        }

        // the top level argument buffer of the next draw or dispatch, everything it points to stays valid until the list completes
        void bind_arguments(metal_encoder::State& state)
        {
            using namespace metal_bindings;

            if (!state.srv_table)
            {
                state.srv_table = upload(state, state.srv.data(), sizeof(state.srv));
            }
            if (!state.uav_table)
            {
                state.uav_table = upload(state, state.uav.data(), sizeof(state.uav));
            }

            Allocation top_level = upload(state, GetTopLevelSize());
            auto write_address   = [&top_level](const Parameter parameter, const uint64_t address)
            {
                memcpy(top_level.data + GetOffset(parameter), &address, sizeof(address));
            };

            write_address(cbv_frame, state.cbv_frame);
            memcpy(top_level.data + GetOffset(push_constants), state.push_constants.data(), GetOffset(srv_table) - GetOffset(push_constants));
            write_address(srv_table, state.srv_table);
            write_address(uav_table, state.uav_table);
            for (uint32_t parameter = material_textures; parameter < parameter_count; parameter++)
            {
                write_address(static_cast<Parameter>(parameter), GetBindlessAddress(static_cast<Parameter>(parameter)));
            }

            state.argument_table->setAddress(top_level.address, index_top_level);
            state.top_level = top_level.address;
        }

        // converted vertex shaders read their draw parameters, vertex and instance ids come from them
        void bind_draw_arguments(metal_encoder::State& state, const uint64_t arguments, const uint16_t index_type)
        {
            state.argument_table->setAddress(arguments, metal_bindings::index_draw_arguments);
            state.argument_table->setAddress(upload(state, &index_type, sizeof(index_type)), metal_bindings::index_draw_uniforms);
        }

        // the converter's index type, 0 for draws without indices
        uint16_t to_ir_index_type(const MTL::IndexType type)
        {
            return static_cast<uint16_t>(type) + 1;
        }

        void apply_render_pipeline(metal_encoder::State& state, const RHI_CullMode cull_mode)
        {
            if (state.pipeline_bound == state.pipeline)
                return;

            metal_pipeline::Pipeline* pipeline  = state.pipeline;
            MTL4::RenderCommandEncoder* encoder = static_cast<MTL4::RenderCommandEncoder*>(state.encoder);
            encoder->setRenderPipelineState(pipeline->render);
            encoder->setDepthStencilState(pipeline->depth_stencil);
            encoder->setTriangleFillMode(pipeline->fill_mode);
            encoder->setDepthClipMode(pipeline->depth_clip_mode);
            encoder->setDepthBias(pipeline->depth_bias, pipeline->depth_bias_slope, pipeline->depth_bias_clamp);
            encoder->setCullMode(pipeline->cull_none ? MTL::CullModeNone : metal_cull_mode[static_cast<uint32_t>(cull_mode)]);
            state.pipeline_bound = pipeline;
        }

        void apply_compute_pipeline(metal_encoder::State& state)
        {
            if (state.pipeline_bound == state.pipeline)
                return;

            static_cast<MTL4::ComputeCommandEncoder*>(state.encoder)->setComputePipelineState(state.pipeline->compute);
            state.pipeline_bound = state.pipeline;
        }

        void release_upload_buffers(metal_encoder::State& state)
        {
            for (MTL::Buffer* buffer : state.upload_buffers)
            {
                metal_residency::Remove(buffer);
                buffer->release();
            }
            state.upload_buffers.clear();
        }

        // clears or loads one slice of a texture through an empty render pass, render pass clears are the fast path on tile gpus
        void clear_with_render_pass(RHI_CommandList* cmd_list, RHI_Texture* texture, const Color& color, const float depth, const uint32_t stencil)
        {
            MTL::Texture* resource                 = static_cast<MTL::Texture*>(texture->GetRhiResource());
            MTL4::RenderPassDescriptor* descriptor = MTL4::RenderPassDescriptor::alloc()->init();

            if (texture->IsDepthStencilFormat())
            {
                MTL::RenderPassDepthAttachmentDescriptor* attachment = descriptor->depthAttachment();
                attachment->setTexture(resource);
                attachment->setLoadAction(MTL::LoadActionClear);
                attachment->setStoreAction(MTL::StoreActionStore);
                attachment->setClearDepth((depth == rhi_depth_load || depth == rhi_depth_dont_care) ? 1.0 : depth);

                if (texture->IsStencilFormat())
                {
                    MTL::RenderPassStencilAttachmentDescriptor* attachment_stencil = descriptor->stencilAttachment();
                    attachment_stencil->setTexture(resource);
                    attachment_stencil->setLoadAction(MTL::LoadActionClear);
                    attachment_stencil->setStoreAction(MTL::StoreActionStore);
                    attachment_stencil->setClearStencil((stencil == rhi_stencil_load || stencil == rhi_stencil_dont_care) ? 0 : stencil);
                }
            }
            else
            {
                MTL::RenderPassColorAttachmentDescriptor* attachment = descriptor->colorAttachments()->object(0);
                attachment->setTexture(resource);
                attachment->setLoadAction(MTL::LoadActionClear);
                attachment->setStoreAction(MTL::StoreActionStore);
                attachment->setClearColor(MTL::ClearColor(color.r, color.g, color.b, color.a));
            }

            metal_encoder::State& state = get_state(cmd_list->GetRhiState());
            metal_encoder::render(cmd_list->GetRhiResource(), state, descriptor);
            metal_encoder::end(state);
            descriptor->release();
        }

        void blit_with_render_pass(RHI_CommandList* cmd_list, MTL::Texture* source, MTL::Texture* destination, MTL::PixelFormat destination_format)
        {
            metal_encoder::State& state = get_state(cmd_list->GetRhiState());

            // depth goes to the depth attachment, color to the first color attachment
            const bool depth                                = metal_blit::is_depth(destination_format);
            MTL4::RenderPassDescriptor* descriptor          = MTL4::RenderPassDescriptor::alloc()->init();
            MTL::RenderPassAttachmentDescriptor* attachment = depth ? static_cast<MTL::RenderPassAttachmentDescriptor*>(descriptor->depthAttachment()) : descriptor->colorAttachments()->object(0);
            attachment->setTexture(destination);
            attachment->setLoadAction(MTL::LoadActionDontCare);
            attachment->setStoreAction(MTL::StoreActionStore);

            MTL4::RenderCommandEncoder* encoder = metal_encoder::render(cmd_list->GetRhiResource(), state, descriptor);
            descriptor->release();

            state.argument_table->setTexture(source->gpuResourceID(), 0);
            encoder->setRenderPipelineState(metal_blit::get_pipeline(destination_format));
            if (depth)
            {
                encoder->setDepthStencilState(metal_blit::get_depth_write());
            }
            encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
            metal_encoder::end(state);
        }
    }

    RHI_CommandList::RHI_CommandList(RHI_Queue* queue, void* cmd_pool, const char* name)
    {
        SP_ASSERT(cmd_pool != nullptr);
        SP_ASSERT(queue != nullptr);

        m_rhi_cmd_pool_resource = cmd_pool;
        m_queue                 = queue;
        m_object_name           = name;

        MTL4::CommandBuffer* command_buffer = RHI_Context::device->newCommandBuffer();
        SP_ASSERT_MSG(command_buffer != nullptr, "Failed to create command buffer");
        metal_utility::set_label(command_buffer, name);
        m_rhi_resource = command_buffer;

        // encoders copy argument table bindings when they draw or dispatch, so one table per list is enough
        MTL4::ArgumentTableDescriptor* descriptor = MTL4::ArgumentTableDescriptor::alloc()->init();
        descriptor->setMaxBufferBindCount(31);
        descriptor->setMaxTextureBindCount(31);
        descriptor->setMaxSamplerStateBindCount(16);
        NS::Error* error            = nullptr;
        metal_encoder::State* state = new metal_encoder::State();
        state->argument_table       = RHI_Context::device->newArgumentTable(descriptor, &error);
        descriptor->release();
        SP_ASSERT_MSG(state->argument_table != nullptr, "Failed to create argument table");
        m_rhi_state = state;

        m_rhi_fence       = RHI_Context::device->newSharedEvent();
        m_rhi_fence_value = 0;

        // gpu timing, a range of a shared timestamp heap, resolved once the list's work has completed
        m_rhi_query_pool_timestamps = timestamps::allocate(m_max_timestamps, state->timestamp_base);

        m_state = RHI_CommandListState::Idle;
        m_rendering_complete_semaphore_timeline = make_shared<RHI_SyncPrimitive>(RHI_SyncPrimitive_Type::SemaphoreTimeline, (string(name) + "_timeline").c_str());
    }

    RHI_CommandList::~RHI_CommandList()
    {
        if (m_state == RHI_CommandListState::Submitted)
        {
            WaitForExecution();
        }

        ReleasePendingWork();

        metal_encoder::State* state = static_cast<metal_encoder::State*>(m_rhi_state);
        release_upload_buffers(*state);
        state->argument_table->release();
        delete state;
        m_rhi_state = nullptr;

        if (m_rhi_query_pool_timestamps)
        {
            static_cast<MTL4::CounterHeap*>(m_rhi_query_pool_timestamps)->release();
            m_rhi_query_pool_timestamps = nullptr;
        }

        static_cast<MTL::SharedEvent*>(m_rhi_fence)->release();
        m_rhi_fence = nullptr;

        static_cast<MTL4::CommandBuffer*>(m_rhi_resource)->release();
        m_rhi_resource = nullptr;

        static_cast<MTL4::CommandAllocator*>(m_rhi_cmd_pool_resource)->release();
        m_rhi_cmd_pool_resource = nullptr;
    }

    void RHI_CommandList::Begin()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Idle);

        // the gpu finished with this allocator's memory, WaitForExecution ran before the list became idle
        MTL4::CommandAllocator* allocator = static_cast<MTL4::CommandAllocator*>(m_rhi_cmd_pool_resource);
        allocator->reset();
        static_cast<MTL4::CommandBuffer*>(m_rhi_resource)->beginCommandBuffer(allocator);

        ResetTrackedResources();

        // the previous recording's timestamps are final, take them before the slots are reused
        ReadbackTimestampsForProfiler();
        m_timestamp_sample.reset();
        m_timestamp_index = 0;

        // the gpu is done with the previous recording, so its bindings and upload memory are free
        metal_encoder::State& state = get_state(m_rhi_state);
        state.srv.fill({});
        state.uav.fill({});
        state.srv_table     = 0;
        state.uav_table     = 0;
        state.cbv_frame     = 0;
        state.pipeline      = nullptr;
        state.upload_index    = 0;
        state.upload_offset   = 0;
        state.barrier_pending = false;
        state.first_encoder   = true;

        m_state                = RHI_CommandListState::Recording;
        m_buffer_id_vertex     = 0;
        m_buffer_id_index      = 0;
        m_render_pass_active   = false;
        m_pso                  = RHI_PipelineState();
        m_pipeline_state_dirty = false;
        m_scissor_valid        = false;
        m_viewport_valid       = false;
        m_vrs_valid            = false;

        Profiler::SubmissionBegin(this, m_queue->GetType());
    }

    void RHI_CommandList::Submit(RHI_SyncPrimitive* semaphore_wait, const bool is_immediate, RHI_SyncPrimitive* semaphore_signal,
                                 RHI_SyncPrimitive* semaphore_timeline_wait, uint64_t timeline_wait_value)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        CommitPendingClear();
        render_pass_end();
        metal_encoder::end(get_state(m_rhi_state));

        Profiler::SubmissionEnd(this);
        static_cast<MTL4::CommandBuffer*>(m_rhi_resource)->endCommandBuffer();

        m_last_timeline_signal_value = m_queue->Submit(m_rhi_resource, 0, semaphore_wait, semaphore_signal, m_rendering_complete_semaphore_timeline.get(), semaphore_timeline_wait, timeline_wait_value, &m_submission_order);
        CommitTrackedResources();

        // a private event, the list knows when its allocator can be reset without asking the timeline
        {
            lock_guard<mutex> lock(metal_queue::GetMutex());
            m_rhi_fence_value++;
            metal_queue::Get()->signalEvent(static_cast<MTL::SharedEvent*>(m_rhi_fence), m_rhi_fence_value);
        }

        if (semaphore_wait)
        {
            semaphore_wait->SetConsumer(GetWork());
        }

        m_state = RHI_CommandListState::Submitted;

        if (is_immediate)
        {
            WaitForExecution();
        }
    }

    void RHI_CommandList::WaitForExecution(const bool log_wait_time)
    {
        if (m_state != RHI_CommandListState::Submitted)
            return;

        static_cast<MTL::SharedEvent*>(m_rhi_fence)->waitUntilSignaledValue(m_rhi_fence_value, UINT64_MAX);
        m_state = RHI_CommandListState::Idle;
    }

    bool RHI_CommandList::IsExecutionComplete()
    {
        if (m_state != RHI_CommandListState::Submitted)
            return m_state == RHI_CommandListState::Idle;

        return static_cast<MTL::SharedEvent*>(m_rhi_fence)->signaledValue() >= m_rhi_fence_value;
    }

    void RHI_CommandList::set_pipeline_state(RHI_PipelineState& pso)
    {
        pso.Prepare();
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_pipeline_state_dirty && m_pso.GetHash() == pso.GetHash() && m_pso.use_standard_resources == pso.use_standard_resources)
            return;

        ResetTrackedBindings();

        // a matching vertex shader and array index means the same attachments continue, so load instead of clear
        const bool same_pass = m_pso.shaders[RHI_Shader_Type::Vertex] != nullptr && m_pso.shaders[RHI_Shader_Type::Vertex] == pso.shaders[RHI_Shader_Type::Vertex] && m_pso.render_target_array_index == pso.render_target_array_index;
        m_load_depth_render_target = same_pass && pso.render_target_depth_texture == m_pso.render_target_depth_texture;
        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
        {
            m_load_color_render_targets[i] = same_pass && pso.render_target_color_textures[i] == m_pso.render_target_color_textures[i];
        }

        RHI_Pipeline* pipeline                         = nullptr;
        RHI_DescriptorSetLayout* descriptor_set_layout = nullptr;
        RHI_Device::GetOrCreatePipeline(pso, pipeline, descriptor_set_layout);

        m_pso         = pso;
        m_pso_pending = pso;
        m_pipeline    = pipeline;
        m_cull_mode   = pso.cull_mode;

        get_state(m_rhi_state).pipeline = pipeline ? static_cast<metal_pipeline::Pipeline*>(pipeline->GetRhiResource()) : nullptr;
        if (pso.IsCompute() || pso.IsRayTracing())
        {
            render_pass_end();
        }
        else
        {
            RenderPassBegin();
        }

        if (pipeline && pipeline->GetRhiResource() && pso.use_standard_resources)
        {
            RHI_Device::InvokePipelineBound(this);
        }

        m_push_constant_size   = 0;
        m_pipeline_state_dirty = !pipeline;
    }

    void RHI_CommandList::RenderPassBegin()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        render_pass_end();
        if (!m_pso.IsGraphics())
            return;

        SynchronizeRenderTargets();

        MTL4::RenderPassDescriptor* descriptor = MTL4::RenderPassDescriptor::alloc()->init();
        uint32_t width                         = 0;
        uint32_t height                        = 0;

        if (RHI_SwapChain* swapchain = m_pso.render_target_swapchain)
        {
            MTL::RenderPassColorAttachmentDescriptor* attachment = descriptor->colorAttachments()->object(0);
            attachment->setTexture(static_cast<MTL::Texture*>(swapchain->GetRhiRt()));
            const bool clear = m_pso.clear_color[0] != rhi_color_load && m_pso.clear_color[0] != rhi_color_dont_care;
            attachment->setLoadAction(clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
            attachment->setStoreAction(MTL::StoreActionStore);
            if (clear)
            {
                attachment->setClearColor(MTL::ClearColor(m_pso.clear_color[0].r, m_pso.clear_color[0].g, m_pso.clear_color[0].b, m_pso.clear_color[0].a));
            }
            width  = swapchain->GetWidth();
            height = swapchain->GetHeight();
        }
        else
        {
            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                RHI_Texture* texture = m_pso.render_target_color_textures[i];
                if (!texture)
                    continue;

                const bool clear = !m_load_color_render_targets[i] && m_pso.clear_color[i] != rhi_color_load && m_pso.clear_color[i] != rhi_color_dont_care;
                MTL::RenderPassColorAttachmentDescriptor* attachment = descriptor->colorAttachments()->object(i);
                attachment->setTexture(static_cast<MTL::Texture*>(texture->GetRhiResource()));
                attachment->setSlice(m_pso.render_target_array_index);
                attachment->setLoadAction(clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
                attachment->setStoreAction(MTL::StoreActionStore);
                if (clear)
                {
                    attachment->setClearColor(MTL::ClearColor(m_pso.clear_color[i].r, m_pso.clear_color[i].g, m_pso.clear_color[i].b, m_pso.clear_color[i].a));
                }
                width  = texture->GetResidentWidth();
                height = texture->GetResidentHeight();
            }
        }

        if (RHI_Texture* depth = m_pso.render_target_depth_texture)
        {
            const bool clear_depth = !m_load_depth_render_target && m_pso.clear_depth != rhi_depth_load && m_pso.clear_depth != rhi_depth_dont_care;
            MTL::RenderPassDepthAttachmentDescriptor* attachment = descriptor->depthAttachment();
            attachment->setTexture(static_cast<MTL::Texture*>(depth->GetRhiResource()));
            attachment->setSlice(m_pso.render_target_array_index);
            attachment->setLoadAction(clear_depth ? MTL::LoadActionClear : MTL::LoadActionLoad);
            attachment->setStoreAction(MTL::StoreActionStore);
            attachment->setClearDepth(clear_depth ? m_pso.clear_depth : 1.0);

            if (depth->IsStencilFormat())
            {
                const bool clear_stencil = !m_load_depth_render_target && m_pso.clear_stencil != rhi_stencil_load && m_pso.clear_stencil != rhi_stencil_dont_care;
                MTL::RenderPassStencilAttachmentDescriptor* attachment_stencil = descriptor->stencilAttachment();
                attachment_stencil->setTexture(static_cast<MTL::Texture*>(depth->GetRhiResource()));
                attachment_stencil->setSlice(m_pso.render_target_array_index);
                attachment_stencil->setLoadAction(clear_stencil ? MTL::LoadActionClear : MTL::LoadActionLoad);
                attachment_stencil->setStoreAction(MTL::StoreActionStore);
                attachment_stencil->setClearStencil(clear_stencil ? m_pso.clear_stencil : 0);
            }

            if (width == 0)
            {
                width  = depth->GetResidentWidth();
                height = depth->GetResidentHeight();
            }
        }

        metal_encoder::State& state = get_state(m_rhi_state);
        metal_encoder::render(m_rhi_resource, state, descriptor, false);
        descriptor->release();
        state.render_pass_width  = width;
        state.render_pass_height = height;

        // the encoder is new, so cached viewport and scissor state is gone
        m_viewport_valid = false;
        m_scissor_valid  = false;
        if (width > 0 && height > 0)
        {
            RHI_Viewport viewport;
            viewport.width  = static_cast<float>(width);
            viewport.height = static_cast<float>(height);
            set_viewport(viewport);
            set_scissor_rectangle(math::Rectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)));
        }

        m_render_pass_active = true;
    }

    void RHI_CommandList::render_pass_end()
    {
        if (!m_render_pass_active)
            return;

        m_render_pass_active = false;
        metal_encoder::end(get_state(m_rhi_state));

        if (m_pso.render_target_swapchain)
            return;

        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
        {
            if (RHI_Texture* texture = m_pso.render_target_color_textures[i])
            {
                SetTrackedTextureLayout(texture, 0, texture->GetResidentMipCount(), RHI_Image_Layout::General);
            }
        }

        if (RHI_Texture* depth = m_pso.render_target_depth_texture)
        {
            SetTrackedTextureLayout(depth, 0, depth->GetResidentMipCount(), RHI_Image_Layout::General);
        }
    }

    void RHI_CommandList::clear_texture(RHI_Texture* texture, const Color& clear_color, const float clear_depth, const uint32_t clear_stencil)
    {
        if (!texture || !texture->GetRhiResource())
            return;

        if (!texture->IsDepthStencilFormat() && (clear_color == rhi_color_load || clear_color == rhi_color_dont_care))
            return;

        // every color texture that can be cleared is created render target capable, see RHI_Texture::RHI_CreateResource
        render_pass_end();
        const RHI_Barrier_Scope scope = texture->IsUav() ? RHI_Barrier_Scope::Compute : RHI_Barrier_Scope::Graphics;
        PrepareForExternalWrite(texture, RHI_Image_Layout::General, scope);
        clear_with_render_pass(this, texture, clear_color, clear_depth, clear_stencil);
        SetTrackedTextureLayout(texture, 0, texture->GetResidentMipCount(), RHI_Image_Layout::General);
        TrackExternalTextureUsage(texture, RHI_Resource_Access::Write, RHI_Image_Layout::General, scope, RHI_Resource_Usage::Attachment);
    }

    // binds the pipeline and writes the arguments of the next draw or dispatch, m_pipeline is null when there is nothing to run
    void RHI_CommandList::PreDraw()
    {
        PrepareDispatch();
        if (!m_pipeline)
            return;

        // ray tracing pipelines are compute pipelines whose kernel is the ray generation shader
        metal_encoder::State& state = get_state(m_rhi_state);
        if (m_pso.IsCompute() || m_pso.IsRayTracing())
        {
            metal_encoder::compute(m_rhi_resource, state, false);
            SynchronizeResources();
            FlushBarriers();
            apply_compute_pipeline(state);
            bind_arguments(state);
            return;
        }

        SynchronizeResources();

        // a copy or clear in the middle of a pass ends its render encoder, the pass continues in a new one that loads
        if (state.type != metal_encoder::Type::Render)
        {
            const RHI_Viewport viewport(m_viewport_x, m_viewport_y, m_viewport_width, m_viewport_height, m_viewport_depth_min, m_viewport_depth_max);
            const math::Rectangle scissor(m_scissor_x, m_scissor_y, m_scissor_width, m_scissor_height);
            m_load_depth_render_target = true;
            m_load_color_render_targets.fill(true);
            RenderPassBegin();
            set_viewport(viewport);
            set_scissor_rectangle(scissor);
        }
        FlushBarriers();

        apply_render_pipeline(state, m_cull_mode);
        bind_arguments(state);
        state.argument_table->setAddress(metal_bindings::GetZeroAddress(), metal_bindings::index_vertex_zero);
        if (state.vertex_buffers[0])
        {
            state.argument_table->setAddress(state.vertex_buffers[0], metal_bindings::index_vertex_buffer);
        }
    }

    void RHI_CommandList::draw(const uint32_t vertex_count, uint32_t vertex_start_index)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        PreDraw();
        if (!m_pipeline)
            return;

        metal_encoder::State& state         = get_state(m_rhi_state);
        MTL4::RenderCommandEncoder* encoder = static_cast<MTL4::RenderCommandEncoder*>(state.encoder);
        const uint32_t arguments[4]         = { vertex_count, 1, vertex_start_index, 0 };
        bind_draw_arguments(state, upload(state, arguments, sizeof(arguments)), 0);
        encoder->drawPrimitives(state.pipeline->primitive_type, vertex_start_index, vertex_count);
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::draw_indexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset, const uint32_t instance_start_index, const uint32_t instance_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        PreDraw();
        if (!m_pipeline)
            return;

        metal_encoder::State& state         = get_state(m_rhi_state);
        MTL4::RenderCommandEncoder* encoder = static_cast<MTL4::RenderCommandEncoder*>(state.encoder);
        SP_ASSERT(state.index_buffer != 0);

        const uint32_t arguments[5] = { index_count, instance_count, index_offset, vertex_offset, instance_start_index };
        bind_draw_arguments(state, upload(state, arguments, sizeof(arguments)), to_ir_index_type(state.index_type));

        const uint64_t index_size = state.index_type == MTL::IndexTypeUInt16 ? 2 : 4;
        const uint64_t offset     = index_offset * index_size;
        encoder->drawIndexedPrimitives(state.pipeline->primitive_type, index_count, state.index_type, state.index_buffer + offset, state.index_buffer_size - offset, instance_count, static_cast<int32_t>(vertex_offset), instance_start_index);
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::draw_indexed_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset, const uint32_t draw_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(args_buffer != nullptr && draw_count != 0);
        TrackBufferRead(3, args_buffer, RHI_Resource_Usage::Indirect);
        PreDraw();
        if (!m_pipeline)
            return;

        // metal draws one indirect command at a time, the layout matches d3d12's draw indexed arguments
        metal_encoder::State& state         = get_state(m_rhi_state);
        MTL4::RenderCommandEncoder* encoder = static_cast<MTL4::RenderCommandEncoder*>(state.encoder);
        const uint64_t stride               = 5 * sizeof(uint32_t);
        const uint64_t arguments            = static_cast<MTL::Buffer*>(args_buffer->GetRhiResource())->gpuAddress() + args_offset;
        for (uint32_t i = 0; i < draw_count; i++)
        {
            bind_draw_arguments(state, arguments + i * stride, to_ir_index_type(state.index_type));
            encoder->drawIndexedPrimitives(state.pipeline->primitive_type, state.index_type, state.index_buffer, state.index_buffer_size, arguments + i * stride);
        }
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::draw_indexed_indirect_count(RHI_Buffer* args_buffer, const uint32_t args_offset, RHI_Buffer* count_buffer, const uint32_t count_offset, const uint32_t max_draw_count)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        TrackBufferRead(3, args_buffer, RHI_Resource_Usage::Indirect);
        TrackBufferRead(4, count_buffer, RHI_Resource_Usage::Indirect);

        // metal has no gpu side draw count, this needs an indirect command buffer
        static once_flag logged;
        call_once(logged, []()
        {
            SP_LOG_WARNING("Indirect draws with a gpu count aren't supported by the metal backend yet");
        });
    }

    void RHI_CommandList::dispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        PreDraw();
        if (!m_pipeline)
            return;

        metal_encoder::State& state          = get_state(m_rhi_state);
        MTL4::ComputeCommandEncoder* encoder = static_cast<MTL4::ComputeCommandEncoder*>(state.encoder);
        encoder->dispatchThreadgroups(MTL::Size(x, y, z), state.pipeline->threadgroup_size);
    }

    void RHI_CommandList::draw_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(args_buffer != nullptr);
        TrackBufferRead(3, args_buffer, RHI_Resource_Usage::Indirect);
        PreDraw();
        if (!m_pipeline)
            return;

        metal_encoder::State& state         = get_state(m_rhi_state);
        MTL4::RenderCommandEncoder* encoder = static_cast<MTL4::RenderCommandEncoder*>(state.encoder);
        const uint64_t arguments            = static_cast<MTL::Buffer*>(args_buffer->GetRhiResource())->gpuAddress() + args_offset;
        bind_draw_arguments(state, arguments, 0);
        encoder->drawPrimitives(state.pipeline->primitive_type, arguments);
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::draw_mesh_tasks_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(args_buffer != nullptr);
        TrackBufferRead(3, args_buffer, RHI_Resource_Usage::Indirect);
        PreDraw();
        if (!m_pipeline)
            return;

        // the arguments are three threadgroup counts, the layout d3d12's dispatch mesh arguments share
        metal_encoder::State& state         = get_state(m_rhi_state);
        MTL4::RenderCommandEncoder* encoder = static_cast<MTL4::RenderCommandEncoder*>(state.encoder);
        const uint64_t arguments            = static_cast<MTL::Buffer*>(args_buffer->GetRhiResource())->gpuAddress() + args_offset;
        encoder->drawMeshThreadgroups(arguments, MTL::Size(1, 1, 1), state.pipeline->threadgroup_size);
        Profiler::m_rhi_draw++;
    }

    void RHI_CommandList::dispatch_indirect(RHI_Buffer* args_buffer, const uint32_t args_offset)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(args_buffer != nullptr);
        TrackBufferRead(3, args_buffer, RHI_Resource_Usage::Indirect);
        PreDraw();
        if (!m_pipeline)
            return;

        metal_encoder::State& state          = get_state(m_rhi_state);
        MTL4::ComputeCommandEncoder* encoder = static_cast<MTL4::ComputeCommandEncoder*>(state.encoder);
        const uint64_t arguments             = static_cast<MTL::Buffer*>(args_buffer->GetRhiResource())->gpuAddress() + args_offset;
        encoder->dispatchThreadgroups(arguments, state.pipeline->threadgroup_size);
    }

    void RHI_CommandList::trace_rays(const uint32_t width, const uint32_t height)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (width == 0 || height == 0)
            return;

        PreDraw();
        if (!m_pipeline)
            return;

        // the kernel finds the shader table, the function table and the global bindings through this argument
        metal_encoder::State& state        = get_state(m_rhi_state);
        metal_pipeline::Pipeline* pipeline = state.pipeline;
        const uint64_t record_size         = sizeof(metal_pipeline::ShaderRecord);
        const uint64_t shader_table        = pipeline->shader_table->gpuAddress();

        IRDispatchRaysArgument arguments                     = {};
        arguments.DispatchRaysDesc.RayGenerationShaderRecord = { shader_table, record_size };
        arguments.DispatchRaysDesc.MissShaderTable           = { shader_table + record_size, record_size, record_size };
        arguments.DispatchRaysDesc.HitGroupTable             = { shader_table + record_size * 2, record_size, record_size };
        arguments.DispatchRaysDesc.Width                     = width;
        arguments.DispatchRaysDesc.Height                    = height;
        arguments.DispatchRaysDesc.Depth                     = 1;
        arguments.GRS                                        = state.top_level;
        arguments.VisibleFunctionTable                       = pipeline->function_table->gpuResourceID();
        state.argument_table->setAddress(upload(state, &arguments, sizeof(arguments)), metal_bindings::index_ray_dispatch);

        static_cast<MTL4::ComputeCommandEncoder*>(state.encoder)->dispatchThreads(MTL::Size(width, height, 1), pipeline->threadgroup_size);
    }

    void RHI_CommandList::set_acceleration_structure(const uint32_t slot, RHI_AccelerationStructure* tlas)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (!tlas || tlas->GetDeviceAddress() == 0 || slot >= rhi_max_resource_slots)
            return;

        // converted shaders reach a tlas through its header, an srv entry holding only the header's address
        metal_encoder::State& state = get_state(m_rhi_state);
        state.srv[slot]             = { tlas->GetDeviceAddress(), 0, 0 };
        state.srv_table             = 0;
    }

    void RHI_CommandList::blit(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips, const float resolution_scale)
    {
        SP_ASSERT(source && destination);
        render_pass_end();

        MTL::Texture* texture_source      = static_cast<MTL::Texture*>(source->GetRhiResource());
        MTL::Texture* texture_destination = static_cast<MTL::Texture*>(destination->GetRhiResource());
        if (!texture_source || !texture_destination)
            return;

        PrepareForExternalRead(source, RHI_Image_Layout::General, RHI_Barrier_Scope::Graphics);
        PrepareForExternalWrite(destination, RHI_Image_Layout::General, RHI_Barrier_Scope::Graphics);

        const bool same_size   = source->GetResidentWidth() == destination->GetResidentWidth() && source->GetResidentHeight() == destination->GetResidentHeight();
        const bool same_format = texture_source->pixelFormat() == texture_destination->pixelFormat();
        if (same_size && same_format)
        {
            const uint32_t mip_count   = blit_mips ? min(source->GetResidentMipCount(), destination->GetResidentMipCount()) : 1;
            const uint32_t layer_count = min(source->GetArrayLength(), destination->GetArrayLength());
            metal_encoder::compute(m_rhi_resource, get_state(m_rhi_state))->copyFromTexture(texture_source, 0, 0, texture_destination, 0, 0, layer_count, mip_count);
        }
        else
        {
            blit_with_render_pass(this, texture_source, texture_destination, texture_destination->pixelFormat());
        }

        SetTrackedTextureLayout(source, 0, source->GetResidentMipCount(), RHI_Image_Layout::General);
        SetTrackedTextureLayout(destination, 0, destination->GetResidentMipCount(), RHI_Image_Layout::General);
    }

    void RHI_CommandList::blit(RHI_Texture* source, RHI_SwapChain* destination)
    {
        SP_ASSERT_MSG((source->GetFlags() & RHI_Texture_ClearBlit) != 0, "The texture needs the RHI_Texture_ClearOrBlit flag");
        render_pass_end();

        MTL::Texture* texture_source      = static_cast<MTL::Texture*>(source->GetRhiResource());
        MTL::Texture* texture_destination = static_cast<MTL::Texture*>(destination->GetRhiRt());
        if (!texture_source || !texture_destination)
            return;

        PrepareForExternalRead(source, RHI_Image_Layout::General, RHI_Barrier_Scope::Graphics);
        blit_with_render_pass(this, texture_source, texture_destination, texture_destination->pixelFormat());
        SetTrackedTextureLayout(source, 0, source->GetResidentMipCount(), RHI_Image_Layout::General);
    }

    void RHI_CommandList::blit_to_array_layer(RHI_Texture* source, RHI_Texture* destination, uint32_t dst_layer)
    {
        SP_ASSERT(source && destination);
        render_pass_end();

        MTL::Texture* texture_source      = static_cast<MTL::Texture*>(source->GetRhiResource());
        MTL::Texture* texture_destination = static_cast<MTL::Texture*>(destination->GetRhiResource());
        if (!texture_source || !texture_destination)
            return;

        PrepareForExternalRead(source, RHI_Image_Layout::General, RHI_Barrier_Scope::Graphics);
        PrepareForExternalWrite(destination, RHI_Image_Layout::General, RHI_Barrier_Scope::Graphics);
        const uint32_t mip_count = min(source->GetResidentMipCount(), destination->GetResidentMipCount());
        metal_encoder::compute(m_rhi_resource, get_state(m_rhi_state))->copyFromTexture(texture_source, 0, 0, texture_destination, dst_layer, 0, 1, mip_count);
    }

    void RHI_CommandList::blit_to_xr_swapchain(RHI_Texture* source)
    {
        // xr is unavailable on metal
    }

    void RHI_CommandList::copy(RHI_Texture* source, RHI_Texture* destination, const bool blit_mips)
    {
        blit(source, destination, blit_mips, 1.0f);
    }

    void RHI_CommandList::copy(RHI_Texture* source, RHI_SwapChain* destination)
    {
        // the backbuffer is bgra while engine textures are rgba, so even a copy has to go through the blit
        blit(source, destination);
    }

    void RHI_CommandList::set_viewport(const RHI_Viewport& viewport) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        m_viewport_x         = viewport.x;
        m_viewport_y         = viewport.y;
        m_viewport_width     = viewport.width;
        m_viewport_height    = viewport.height;
        m_viewport_depth_min = viewport.depth_min;
        m_viewport_depth_max = viewport.depth_max;
        m_viewport_valid     = true;

        metal_encoder::State& state = get_state(m_rhi_state);
        if (state.type == metal_encoder::Type::Render)
        {
            MTL::Viewport viewport_metal = { viewport.x, viewport.y, viewport.width, viewport.height, viewport.depth_min, viewport.depth_max };
            static_cast<MTL4::RenderCommandEncoder*>(state.encoder)->setViewport(viewport_metal);
        }
    }

    void RHI_CommandList::set_scissor_rectangle(const math::Rectangle& scissor_rectangle) const
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        m_scissor_x      = scissor_rectangle.x;
        m_scissor_y      = scissor_rectangle.y;
        m_scissor_width  = scissor_rectangle.width;
        m_scissor_height = scissor_rectangle.height;
        m_scissor_valid  = true;

        metal_encoder::State& state = get_state(m_rhi_state);
        if (state.type == metal_encoder::Type::Render)
        {
            // d3d12 and vulkan clip a scissor to the attachments, metal requires it to fit, so it's clamped here
            const float width  = static_cast<float>(state.render_pass_width);
            const float height = static_cast<float>(state.render_pass_height);
            const float left   = clamp(scissor_rectangle.x, 0.0f, width);
            const float top    = clamp(scissor_rectangle.y, 0.0f, height);
            const float right  = clamp(scissor_rectangle.x + scissor_rectangle.width, left, width);
            const float bottom = clamp(scissor_rectangle.y + scissor_rectangle.height, top, height);
            MTL::ScissorRect scissor =
            {
                static_cast<NS::UInteger>(left),
                static_cast<NS::UInteger>(top),
                static_cast<NS::UInteger>(right - left),
                static_cast<NS::UInteger>(bottom - top)
            };
            static_cast<MTL4::RenderCommandEncoder*>(state.encoder)->setScissorRect(scissor);
        }
    }

    void RHI_CommandList::set_cull_mode(const RHI_CullMode cull_mode)
    {
        m_cull_mode = cull_mode;

        metal_encoder::State& state = get_state(m_rhi_state);
        if (state.type == metal_encoder::Type::Render)
        {
            static_cast<MTL4::RenderCommandEncoder*>(state.encoder)->setCullMode(metal_cull_mode[static_cast<uint32_t>(cull_mode)]);
        }
    }

    void RHI_CommandList::set_buffer_vertex(const RHI_Buffer* vertex, RHI_Buffer* instance)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(vertex && vertex->GetRhiResource());
        TrackBufferRead(0, const_cast<RHI_Buffer*>(vertex), RHI_Resource_Usage::Vertex);
        TrackBufferRead(1, instance, RHI_Resource_Usage::Vertex);

        metal_encoder::State& state = get_state(m_rhi_state);
        state.vertex_buffers[0]     = static_cast<MTL::Buffer*>(vertex->GetRhiResource())->gpuAddress();
        state.vertex_buffers[1]     = instance && instance->GetRhiResource() ? static_cast<MTL::Buffer*>(instance->GetRhiResource())->gpuAddress() : 0;
        m_buffer_id_vertex          = vertex->GetObjectId();
        m_buffer_id_instance        = instance ? instance->GetObjectId() : 0;
        Profiler::m_rhi_bindings_buffer_vertex++;
    }

    void RHI_CommandList::set_buffer_index(const RHI_Buffer* buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(buffer && buffer->GetRhiResource());
        TrackBufferRead(2, const_cast<RHI_Buffer*>(buffer), RHI_Resource_Usage::Index);

        metal_encoder::State& state = get_state(m_rhi_state);
        state.index_buffer          = static_cast<MTL::Buffer*>(buffer->GetRhiResource())->gpuAddress();
        state.index_buffer_size     = buffer->GetObjectSize();
        state.index_type            = buffer->GetStride() == sizeof(uint16_t) ? MTL::IndexTypeUInt16 : MTL::IndexTypeUInt32;
        m_buffer_id_index           = buffer->GetObjectId();
        Profiler::m_rhi_bindings_buffer_index++;
    }

    void RHI_CommandList::set_constant_buffer(const uint32_t slot, RHI_Buffer* constant_buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (!constant_buffer || !constant_buffer->GetRhiResource())
            return;

        // the root signature has a single constant buffer, the frame buffer at b0
        get_state(m_rhi_state).cbv_frame = static_cast<MTL::Buffer*>(constant_buffer->GetRhiResource())->gpuAddress() + constant_buffer->GetOffset();
        m_constant_buffer_bound          = true;
    }

    void RHI_CommandList::push_constants(const uint32_t offset, const uint32_t size, const void* data)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        TryBindPendingPipeline();
        if (!m_pipeline)
            return;

        SP_ASSERT(data != nullptr && offset + size <= metal_bindings::push_constants_size_max);
        memcpy(get_state(m_rhi_state).push_constants.data() + offset, data, size);
        m_push_constant_size = size;
    }

    void RHI_CommandList::set_buffer(const uint32_t slot, RHI_Buffer* buffer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        RHI_Resource_Access access = GetBufferAccess(slot);
        if (buffer && buffer->GetRhiResource() && access == RHI_Resource_Access::None)
        {
            access = RHI_Resource_Access::Read;
        }
        TrackBufferUsage(slot, buffer && buffer->GetRhiResource() ? buffer : nullptr, access);
        if (!buffer || !buffer->GetRhiResource() || access == RHI_Resource_Access::None || slot >= rhi_max_resource_slots)
            return;

        // reads go to the t registers, writes to the u registers
        metal_encoder::State& state                = get_state(m_rhi_state);
        const bool read                            = access == RHI_Resource_Access::Read;
        (read ? state.srv : state.uav)[slot]       = metal_bindings::buffer(static_cast<MTL::Buffer*>(buffer->GetRhiResource()), 0, buffer->GetObjectSize());
        (read ? state.srv_table : state.uav_table) = 0;
    }

    void RHI_CommandList::set_texture(const uint32_t slot, RHI_Texture* texture, const uint32_t mip_index, uint32_t mip_range, const bool uav, const uint32_t array_layer)
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!IsTextureBindingUsed(slot, uav))
            return;

        TrackTextureUsage(slot, texture, mip_index, mip_range, array_layer, uav);
        if (slot >= rhi_max_resource_slots)
            return;

        metal_encoder::State& state                                      = get_state(m_rhi_state);
        array<metal_bindings::Descriptor, rhi_max_resource_slots>& table = uav ? state.uav : state.srv;
        (uav ? state.uav_table : state.srv_table)                        = 0;
        if (!texture || !texture->GetRhiResource())
        {
            table[slot] = {};
            return;
        }

        // a uav sees one mip, a mip range of uavs (spd) is an array of views over consecutive u registers
        const bool mip_specified = mip_index != rhi_all_mips;
        const uint32_t mip_first = mip_specified ? mip_index : 0;
        const uint32_t mip_count = mip_specified ? max(mip_range, 1u) : (uav ? 1 : texture->GetResidentMipCount());
        if (uav && mip_count > 1)
        {
            SP_ASSERT(slot + mip_count <= rhi_max_resource_slots);
            for (uint32_t i = 0; i < mip_count; i++)
            {
                table[slot + i] = metal_bindings::texture(metal_texture::GetView(texture, mip_first + i, 1, array_layer, true));
            }
            return;
        }

        table[slot] = metal_bindings::texture(metal_texture::GetView(texture, mip_first, mip_count, array_layer, uav));
    }

    uint32_t RHI_CommandList::begin_timestamp()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        if (!cvar_debug_gpu_timing.GetValue() || !m_rhi_query_pool_timestamps)
            return UINT32_MAX;

        if (m_timestamp_index >= m_max_timestamps)
        {
            Profiler::m_rhi_timestamps_dropped++;
            return UINT32_MAX;
        }

        // written between encoders, timestamps written inside one sometimes never land, a pass interrupted
        // here continues in a new encoder that loads its attachments
        metal_encoder::State& state = get_state(m_rhi_state);
        metal_encoder::end(state);
        static_cast<MTL4::CommandBuffer*>(m_rhi_resource)->writeTimestampIntoHeap(static_cast<MTL4::CounterHeap*>(m_rhi_query_pool_timestamps), state.timestamp_base + m_timestamp_index);

        return m_timestamp_index++;
    }

    // a timestamp is a point in time, beginning and ending a block write the same kind
    uint32_t RHI_CommandList::end_timestamp()
    {
        return begin_timestamp();
    }

    void RHI_CommandList::ReadbackTimestampsForProfiler()
    {
        if (!m_timestamp_sample || m_timestamp_sample->ready || !GetWork().IsComplete())
            return;

        const uint32_t count = min(m_timestamp_index, m_max_timestamps);
        if (count > 0)
        {
            // resolving returns autoreleased data, engine threads have no pool to drain it into
            NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
            NS::Data* data            = static_cast<MTL4::CounterHeap*>(m_rhi_query_pool_timestamps)->resolveCounterRange(NS::Range(get_state(m_rhi_state).timestamp_base, count));
            if (data)
            {
                // metal-cpp has no accessor for immutable data, nsdata is toll free bridged with cfdata
                const MTL4::TimestampHeapEntry* entries = reinterpret_cast<const MTL4::TimestampHeapEntry*>(CFDataGetBytePtr(reinterpret_cast<CFDataRef>(data)));
                for (uint32_t i = 0; i < count; i++)
                {
                    m_timestamp_sample->ticks[i]     = entries[i].timestamp;
                    m_timestamp_sample->available[i] = true;
                }
            }
            pool->release();
            m_timestamp_sample->count = count;
        }

        m_timestamp_sample->ready = true;
    }

    void RHI_CommandList::begin_occlusion_query(const uint64_t entity_id)
    {
    }

    void RHI_CommandList::end_occlusion_query()
    {
    }

    bool RHI_CommandList::GetOcclusionQueryResult(const uint64_t entity_id)
    {
        // visible, so nothing gets culled by a query that never ran
        return true;
    }

    void RHI_CommandList::update_occlusion_queries()
    {
    }

    void RHI_CommandList::begin_timeblock(const char* name, const bool gpu_marker, const bool gpu_timing)
    {
        SP_ASSERT(name != nullptr);

        RHI_Queue_Type queue_type = m_queue ? m_queue->GetType() : RHI_Queue_Type::Max;
        Profiler::TimeBlockStart(name, TimeBlockType::Cpu, this, queue_type);
        const bool time_gpu = cvar_debug_gpu_timing.GetValue() && gpu_timing;
        if (time_gpu)
        {
            Profiler::TimeBlockStart(name, TimeBlockType::Gpu, this, queue_type);
        }
        m_timeblock_gpu_timing.push(time_gpu);
        get_state(m_rhi_state).pass_name = name;

        if (cvar_debug_gpu_marking.GetValue() && gpu_marker)
        {
            RHI_Device::MarkerBegin(this, name, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        }
    }

    void RHI_CommandList::end_timeblock()
    {
        if (cvar_debug_gpu_marking.GetValue())
        {
            RHI_Device::MarkerEnd(this);
        }

        if (!m_timeblock_gpu_timing.empty())
        {
            if (m_timeblock_gpu_timing.top())
            {
                Profiler::TimeBlockEnd(TimeBlockType::Gpu, this);
            }
            m_timeblock_gpu_timing.pop();
        }

        Profiler::TimeBlockEnd(TimeBlockType::Cpu, this);
    }

    void RHI_CommandList::begin_marker(const char* name)
    {
        if (cvar_debug_gpu_marking.GetValue())
        {
            RHI_Device::MarkerBegin(this, name, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        }
    }

    void RHI_CommandList::end_marker()
    {
        if (cvar_debug_gpu_marking.GetValue())
        {
            RHI_Device::MarkerEnd(this);
        }
    }

    void RHI_CommandList::write_gpu_breadcrumb(RHI_Buffer* buffer, uint32_t slot, uint32_t value)
    {
        SP_ASSERT(buffer && buffer->GetRhiResource());
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // breadcrumb buffers are mappable, a fill writes the value without a staging copy
        render_pass_end();
        MTL::Buffer* resource                = static_cast<MTL::Buffer*>(buffer->GetRhiResource());
        MTL4::ComputeCommandEncoder* encoder = metal_encoder::compute(m_rhi_resource, get_state(m_rhi_state));
        for (uint32_t i = 0; i < sizeof(uint32_t); i++)
        {
            encoder->fillBuffer(resource, NS::Range(slot * sizeof(uint32_t) + i, 1), static_cast<uint8_t>(value >> (i * 8)));
        }
    }

    void RHI_CommandList::update_buffer(RHI_Buffer* buffer, const uint64_t offset, const uint64_t size, const void* data, const bool use_mapped_memory)
    {
        if (!buffer || !data || size == 0)
            return;

        if (use_mapped_memory && buffer->GetMappedData())
        {
            memcpy(static_cast<uint8_t*>(buffer->GetMappedData()) + offset, data, static_cast<size_t>(size));
            return;
        }

        MTL::Buffer* destination = static_cast<MTL::Buffer*>(buffer->GetRhiResource());
        MTL::Buffer* staging     = static_cast<MTL::Buffer*>(RHI_Device::StagingBufferAcquire(size));
        if (!destination || !staging)
            return;

        memcpy(staging->contents(), data, static_cast<size_t>(size));

        render_pass_end();
        metal_encoder::compute(m_rhi_resource, get_state(m_rhi_state))->copyFromBuffer(staging, 0, destination, offset, size);

        // the staging buffer goes back to the pool once this list completes
        RetainStagingBuffer(staging);
    }

    void RHI_CommandList::InsertBarrier(const RHI_Barrier& barrier)
    {
        // layouts don't exist on metal, only execution and memory ordering, which the next FlushBarriers provides
        m_force_memory_sync                    = true;
        get_state(m_rhi_state).barrier_pending = true;
    }

    void RHI_CommandList::FlushBarriers()
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_force_memory_sync)
            return;

        // without an open encoder the barrier stays pending and the next encoder starts with it
        metal_encoder::State& state = get_state(m_rhi_state);
        if (state.encoder)
        {
            metal_encoder::barrier(state);
        }

        m_force_memory_sync = false;
        MarkTrackedResourcesSynced();
    }

    void RHI_CommandList::restore_after_external_pass()
    {
        m_pipeline_state_dirty = true;
    }

    void RHI_CommandList::EnsureComputeShaderResource(RHI_Texture* texture, bool include_pixel_stage)
    {
        if (!texture || m_state != RHI_CommandListState::Recording)
            return;

        SetTrackedTextureLayout(texture, 0, texture->GetResidentMipCount(), RHI_Image_Layout::General);
    }

    void RHI_CommandList::AdoptComputeShaderResource(RHI_Texture* texture, bool include_pixel_stage)
    {
        if (!texture || m_state != RHI_CommandListState::Recording)
            return;

        SetTrackedTextureLayout(texture, 0, texture->GetResidentMipCount(), RHI_Image_Layout::General);
        TrackExternalTextureUsage(texture, RHI_Resource_Access::Read, RHI_Image_Layout::General, RHI_Barrier_Scope::Compute);
    }

    void RHI_CommandList::AdoptUnorderedAccess(RHI_Texture* texture)
    {
        if (!texture || m_state != RHI_CommandListState::Recording)
            return;

        SetTrackedTextureLayout(texture, 0, texture->GetResidentMipCount(), RHI_Image_Layout::General);
        TrackExternalTextureUsage(texture, RHI_Resource_Access::Write, RHI_Image_Layout::General, RHI_Barrier_Scope::Compute);
    }

    void RHI_CommandList::InsertBarrier(RHI_Texture* texture, RHI_Image_Layout layout, uint32_t mip, uint32_t mip_range)
    {
        InsertBarrier(RHI_Barrier::image_layout(texture, layout, mip, mip_range));
    }

    void RHI_CommandList::InsertBarrier(RHI_Buffer* buffer)
    {
        InsertBarrier(RHI_Barrier::buffer_sync(buffer));
    }

    void RHI_CommandList::InsertBarrier(void* image, RHI_Format format, uint32_t mip_index, uint32_t mip_range, uint32_t array_length, RHI_Image_Layout layout)
    {
        InsertBarrier(RHI_Barrier::image_layout(image, format, mip_index, mip_range, array_length, layout));
    }

    void RHI_CommandList::RemoveLayout(void* image)
    {
    }

    RHI_Image_Layout RHI_CommandList::GetImageLayout(void* image, const uint32_t mip_index)
    {
        return image ? RHI_Image_Layout::General : RHI_Image_Layout::Max;
    }

    void RHI_CommandList::copy_texture_to_buffer(RHI_Texture* source, RHI_Buffer* destination)
    {
        SP_ASSERT(source && destination);
        render_pass_end();

        MTL::Texture* texture_source    = static_cast<MTL::Texture*>(source->GetRhiResource());
        MTL::Buffer* buffer_destination = static_cast<MTL::Buffer*>(destination->GetRhiResource());
        if (!texture_source || !buffer_destination)
            return;

        PrepareForExternalRead(source, RHI_Image_Layout::General, RHI_Barrier_Scope::Compute);

        const uint32_t width         = source->GetResidentWidth();
        const uint32_t height        = source->GetResidentHeight();
        const uint32_t bytes_per_row = width * source->GetBytesPerPixel();
        metal_encoder::compute(m_rhi_resource, get_state(m_rhi_state))->copyFromTexture(
            texture_source, 0, 0, MTL::Origin(0, 0, 0), MTL::Size(width, height, 1),
            buffer_destination, 0, bytes_per_row, bytes_per_row * height
        );
    }

    void RHI_CommandList::copy_buffer_to_buffer(void* source, RHI_Buffer* destination, uint64_t size)
    {
        SP_ASSERT(source && destination);
        render_pass_end();
        metal_encoder::compute(m_rhi_resource, get_state(m_rhi_state))->copyFromBuffer(static_cast<MTL::Buffer*>(source), 0, static_cast<MTL::Buffer*>(destination->GetRhiResource()), 0, size);
    }

    void RHI_CommandList::copy_buffer_to_buffer(RHI_Buffer* source, RHI_Buffer* destination, uint64_t size)
    {
        SP_ASSERT(source);
        copy_buffer_to_buffer(source->GetRhiResource(), destination, size);
    }

    namespace immediate_execution
    {
        static const uint32_t queue_type_count = static_cast<uint32_t>(RHI_Queue_Type::Max);

        array<mutex, queue_type_count>                 mutexes;
        array<condition_variable, queue_type_count>    condition_vars;
        array<bool, queue_type_count>                  is_executing = {};
        array<shared_ptr<RHI_Queue>, queue_type_count> queues;
        once_flag init_flag;

        void ensure_initialized()
        {
            call_once(init_flag, []()
            {
                queues[static_cast<uint32_t>(RHI_Queue_Type::Graphics)] = make_shared<RHI_Queue>(RHI_Queue_Type::Graphics, "graphics_immediate");
                queues[static_cast<uint32_t>(RHI_Queue_Type::Compute)]  = make_shared<RHI_Queue>(RHI_Queue_Type::Compute,  "compute_immediate");
                queues[static_cast<uint32_t>(RHI_Queue_Type::Copy)]     = make_shared<RHI_Queue>(RHI_Queue_Type::Copy,     "copy_immediate");
            });
        }
    }

    RHI_CommandList* RHI_CommandList::ImmediateExecutionBegin(const RHI_Queue_Type queue_type)
    {
        if (RHI_Device::IsDeviceLost())
            return nullptr;

        immediate_execution::ensure_initialized();

        const uint32_t queue_index = static_cast<uint32_t>(queue_type);
        unique_lock<mutex> lock(immediate_execution::mutexes[queue_index]);
        immediate_execution::condition_vars[queue_index].wait(lock, [queue_index]()
        {
            return !immediate_execution::is_executing[queue_index];
        });
        immediate_execution::is_executing[queue_index] = true;

        RHI_CommandList* cmd_list = immediate_execution::queues[queue_index]->NextCommandList();
        cmd_list->Begin();
        cmd_list->m_previous_binding = RHI_Device::Cmd();
        RHI_Device::Bind(cmd_list);
        return cmd_list;
    }

    void RHI_CommandList::ImmediateExecutionEnd(RHI_CommandList* cmd_list, bool wait)
    {
        if (!cmd_list)
            return;

        cmd_list->Submit(nullptr, wait);

        const uint32_t queue_index = static_cast<uint32_t>(cmd_list->GetQueue()->GetType());

        if (!wait)
        {
            SP_ASSERT(cmd_list->GetQueue()->GetType() == RHI_Queue_Type::Graphics);
            SetPendingUpload(cmd_list->GetWork());
        }

        RHI_Device::Bind(cmd_list->m_previous_binding);

        {
            lock_guard<mutex> lock(immediate_execution::mutexes[queue_index]);
            immediate_execution::is_executing[queue_index] = false;
        }
        immediate_execution::condition_vars[queue_index].notify_one();
    }

    void RHI_CommandList::ImmediateExecutionShutdown()
    {
        for (uint32_t i = 0; i < immediate_execution::queue_type_count; i++)
        {
            unique_lock<mutex> lock(immediate_execution::mutexes[i]);
            immediate_execution::condition_vars[i].wait(lock, [i]()
            {
                return !immediate_execution::is_executing[i];
            });
        }

        immediate_execution::queues.fill(nullptr);
        SetPendingUpload({});
        metal_blit::destroy();
        timestamps::destroy();
    }

    void* RHI_CommandList::GetRhiResourcePipeline()
    {
        return m_pipeline ? m_pipeline->GetRhiResource() : nullptr;
    }
}
