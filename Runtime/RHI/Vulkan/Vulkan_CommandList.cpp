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

//= IMPLEMENTATION ===============
#ifdef API_GRAPHICS_VULKAN
#include "../RHI_Implementation.h"
//================================

//= INCLUDES ========================
#include "../RHI_CommandList.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Device.h"
#include "../RHI_SwapChain.h"
#include "../RHI_Sampler.h"
#include "../RHI_Texture.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_PipelineState.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_DescriptorCache.h"
#include "../RHI_PipelineCache.h"
#include "../../Profiling/Profiler.h"
#include "../../Logging/Log.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    RHI_CommandList::RHI_CommandList(uint32_t index, RHI_SwapChain* swap_chain, Context* context)
	{
        m_swap_chain        = swap_chain;
        m_renderer          = context->GetSubsystem<Renderer>();
        m_profiler          = context->GetSubsystem<Profiler>();
		m_rhi_device	    = m_renderer->GetRhiDevice().get();
        m_pipeline_cache    = m_renderer->GetPipelineCache();
        m_descriptor_cache  = m_renderer->GetDescriptorCache();
        m_passes_active.reserve(100);
        m_passes_active.resize(100);
        m_timestamps.reserve(2);
        m_timestamps.resize(2);

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Query pool
        if (rhi_context->profiler)
        {
            VkQueryPoolCreateInfo query_pool_create_info    = {};
            query_pool_create_info.sType                    = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            query_pool_create_info.queryType                = VK_QUERY_TYPE_TIMESTAMP;
            query_pool_create_info.queryCount               = static_cast<uint32_t>(m_timestamps.size());

            auto query_pool = reinterpret_cast<VkQueryPool*>(&m_query_pool);
            vulkan_utility::error::check(vkCreateQueryPool(rhi_context->device, &query_pool_create_info, nullptr, query_pool));
        }

        // Command buffer
        vulkan_utility::command_buffer::create(m_swap_chain->GetCmdPool(), m_cmd_buffer, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

        // Fence
        vulkan_utility::fence::create(m_cmd_list_consumed_fence);
	}

	RHI_CommandList::~RHI_CommandList()
	{
        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

		// Wait in case the buffer is still in use by the graphics queue
        m_rhi_device->Queue_Wait(RHI_Queue_Graphics);

		// Fence
        vulkan_utility::fence::destroy(m_cmd_list_consumed_fence);

        // Command buffer
        vulkan_utility::command_buffer::destroy(m_swap_chain->GetCmdPool(), m_cmd_buffer);

        // Query pool
        if (m_query_pool)
        {
            vkDestroyQueryPool(rhi_context->device, static_cast<VkQueryPool>(m_query_pool), nullptr);
            m_query_pool = nullptr;
        }
	}

    bool RHI_CommandList::StartRecording()
    {
        // Sync CPU to GPU
        if (m_cmd_state == RHI_Cmd_List_Idle_Sync_Cpu_To_Gpu)
        {
            Wait();
            m_descriptor_cache->GrowIfNeeded();
            m_cmd_state = RHI_Cmd_List_Idle;
        }

        if (m_cmd_state != RHI_Cmd_List_Idle)
        {
            LOG_ERROR("The command list is still being used");
            return false;
        }

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!vulkan_utility::error::check(vkBeginCommandBuffer(static_cast<VkCommandBuffer>(m_cmd_buffer), &begin_info)))
            return false;

        m_cmd_state = RHI_Cmd_List_Recording;
        return true;
    }

    bool RHI_CommandList::StopRecording()
    {
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_ERROR("The command list is not recording");
            return false;
        }

        if (!vulkan_utility::error::check(vkEndCommandBuffer(static_cast<VkCommandBuffer>(m_cmd_buffer))))
            return false;

        m_cmd_state = RHI_Cmd_List_Idle;
        return true;
    }

    bool RHI_CommandList::Submit()
    {
        if (m_cmd_state != RHI_Cmd_List_Idle)
        {
            LOG_ERROR("Can't submit while the command list is recording.");
            return false;
        }

        RHI_PipelineState* state = m_pipeline->GetPipelineState();

        if (!m_rhi_device->Queue_Submit(
            RHI_Queue_Graphics,                                                                                                                         // queue
            static_cast<VkCommandBuffer>(m_cmd_buffer),                                                                                                 // cmd buffer
            state->render_target_swapchain ? static_cast<VkSemaphore>(state->render_target_swapchain->Get_Resource_View_AcquiredSemaphore()) : nullptr, // wait semaphore
            m_cmd_list_consumed_fence,                                                                                                                  // wait fence
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)                                                                                              // wait flags
        )
            return false;

        // If the render pass has been bound then it cleared to whatever values was requested (or not)
        // So at this point we reset the values as we don't want to clear again.
        if (m_render_pass_active)
        {
            m_pipeline_state->ResetClearValues();
        }

        // Wait for fence on the next Begin(), if we force it now, perfomance will not be as good
        m_cmd_state = RHI_Cmd_List_Idle_Sync_Cpu_To_Gpu;

        return true;
    }

    bool RHI_CommandList::Wait()
    {
        return vulkan_utility::fence::wait_reset(m_cmd_list_consumed_fence);
    }

    bool RHI_CommandList::BeginPass(RHI_PipelineState& pipeline_state)
	{
        // Get pipeline
        {
            m_pipeline_active = false;

            // Update the descriptor cache with the pipeline state and potentially create a new pipeline (if not already there)
            m_descriptor_cache->SetPipelineState(pipeline_state);

            // Get a pipeline which matches the pipeline state
            m_pipeline = m_pipeline_cache->GetPipeline(this, pipeline_state, m_descriptor_cache->GetResource_DescriptorSetLayout());
            if (!m_pipeline)
            {
                LOG_ERROR("Failed to acquire appropriate pipeline");
                End();
                return false;
            }

            // Acquire next image (in case the render target is a swapchain)
            if (RHI_SwapChain* swapchain = m_pipeline->GetPipelineState()->render_target_swapchain)
            {
                if (!swapchain->AcquireNextImage())
                {
                    LOG_ERROR("Failed to acquire next image");
                    End();
                    return false;
                }
            }

            // Keep a local pointer for convenience
            m_pipeline_state = &pipeline_state;
        }

        // Start marker and profiler (if used)
        MarkAndProfileStart(m_pipeline_state);

        // Shader resources
        {
            // If the pipeline changed, we are using new descriptors, so the resources have to be set again
            m_set_id_buffer_vertex  = 0;
            m_set_id_buffer_pixel   = 0;

            // Vulkan doesn't have a persistent state so global resources have to be set
            m_renderer->SetGlobalSamplersAndConstantBuffers(this);
        }

        return true;
	}

    bool RHI_CommandList::EndPass()
    {
        if (m_render_pass_active)
        {
            vkCmdEndRenderPass(static_cast<VkCommandBuffer>(m_cmd_buffer));
            m_render_pass_active = false;
        }

        // End marker and profiler
        MarkAndProfileEnd(m_pipeline_state);
       
        return true;
    }

    void RHI_CommandList::Clear(RHI_PipelineState& pipeline_state)
    {
        if (Begin(pipeline_state))
        {
            OnDraw();
            End();
            Submit();
            pipeline_state.ResetClearValues();
        }
    }

	void RHI_CommandList::Draw(const uint32_t vertex_count)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        // Ensure correct state before attempting to draw
        if (!OnDraw())
            return;

		vkCmdDraw(
            static_cast<VkCommandBuffer>(m_cmd_buffer),     // commandBuffer
            vertex_count,   // vertexCount
            1,              // instanceCount
            0,              // firstVertex
            0               // firstInstance
        );

        m_profiler->m_rhi_draw_calls++;
	}

	void RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        // Ensure correct state before attempting to draw
        if (!OnDraw())
            return;

		vkCmdDrawIndexed(
            static_cast<VkCommandBuffer>(m_cmd_buffer),     // commandBuffer
            index_count,    // indexCount
            1,              // instanceCount
            index_offset,   // firstIndex
            vertex_offset,  // vertexOffset
            0               // firstInstance
        );

        m_profiler->m_rhi_draw_calls++;
	}

    void RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z /*= 1*/) const
    {
        
    }

	void RHI_CommandList::SetViewport(const RHI_Viewport& viewport) const
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

		VkViewport vk_viewport	= {};
		vk_viewport.x			= viewport.x;
		vk_viewport.y			= viewport.y;
		vk_viewport.width		= viewport.width;
		vk_viewport.height		= viewport.height;
		vk_viewport.minDepth	= viewport.depth_min;
		vk_viewport.maxDepth	= viewport.depth_max;

		vkCmdSetViewport(
            static_cast<VkCommandBuffer>(m_cmd_buffer),     // commandBuffer
            0,              // firstViewport
            1,              // viewportCount
            &vk_viewport    // pViewports
        );
	}

	void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle) const
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

		VkRect2D vk_scissor;
		vk_scissor.offset.x			= static_cast<int32_t>(scissor_rectangle.left);
		vk_scissor.offset.y			= static_cast<int32_t>(scissor_rectangle.top);
		vk_scissor.extent.width		= static_cast<uint32_t>(scissor_rectangle.Width());
		vk_scissor.extent.height	= static_cast<uint32_t>(scissor_rectangle.Height());

		vkCmdSetScissor(
            static_cast<VkCommandBuffer>(m_cmd_buffer), // commandBuffer
            0,          // firstScissor
            1,          // scissorCount
            &vk_scissor // pScissors
        );
	}

	void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        if (m_set_id_buffer_vertex == buffer->GetId())
            return;

		VkBuffer vertex_buffers[]	= { static_cast<VkBuffer>(buffer->GetResource()) };
		VkDeviceSize offsets[]		= { 0 };

		vkCmdBindVertexBuffers(
            static_cast<VkCommandBuffer>(m_cmd_buffer),     // commandBuffer
            0,              // firstBinding
            1,              // bindingCount
            vertex_buffers, // pBuffers
            offsets         // pOffsets
        );

        m_profiler->m_rhi_bindings_buffer_vertex++;
        m_set_id_buffer_vertex = buffer->GetId();
	}

	void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer)
	{
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        if (m_set_id_buffer_pixel == buffer->GetId())
            return;

		vkCmdBindIndexBuffer(
			static_cast<VkCommandBuffer>(m_cmd_buffer),                                                     // commandBuffer
			static_cast<VkBuffer>(buffer->GetResource()),					// buffer
			0,																// offset
			buffer->Is16Bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32 // indexType
		);

        m_profiler->m_rhi_bindings_buffer_index++;
        m_set_id_buffer_pixel = buffer->GetId();
	}

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, const uint8_t scope, RHI_ConstantBuffer* constant_buffer) const
    {
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        // Set (will only happen if it's not already set)
        m_descriptor_cache->SetConstantBuffer(slot, constant_buffer);
    }

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler) const
    {
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        // Set (will only happen if it's not already set)
        m_descriptor_cache->SetSampler(slot, sampler);
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture, const uint8_t scope /*= RHI_Shader_Pixel*/)
    {
        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return;
        }

        // Null textures are allowed, and get replaced with a black texture here
        if (!texture || !texture->Get_Resource_View())
        {
            texture = m_renderer->GetBlackTexture();
        }

        // If the image has an invalid layout (can happen for a few frames during staging), replace with black
        if (texture->GetLayout() == RHI_Image_Undefined || texture->GetLayout() == RHI_Image_Preinitialized)
        {
            LOG_WARNING("Can't set texture without a layout");
            texture = m_renderer->GetBlackTexture();
        }

        // Transition to appropriate layout (if needed)
        {
            RHI_Image_Layout target_layout = RHI_Image_Undefined;

            // Color
            if (texture->IsColorFormat() && texture->GetLayout() != RHI_Image_Shader_Read_Only_Optimal)
            {
                target_layout = RHI_Image_Shader_Read_Only_Optimal;
            }

            // Depth
            if (texture->IsDepthFormat() && texture->GetLayout() != RHI_Image_Depth_Stencil_Read_Only_Optimal)
            {
                target_layout = RHI_Image_Depth_Stencil_Read_Only_Optimal;
            }

            bool transition_required = target_layout != RHI_Image_Undefined;

            // Transition
            if (transition_required && !m_render_pass_active)
            {
                texture->SetLayout(target_layout, this);
            }
            else if (transition_required && m_render_pass_active)
            {
                LOG_WARNING("Can't transition texture to target layout while a render pass is active");
                texture = m_renderer->GetBlackTexture();
            }
        }

        // Set (will only happen if it's not already set)
        m_descriptor_cache->SetTexture(slot, texture);
    }

    uint32_t RHI_CommandList::Gpu_GetMemory(RHI_Device* rhi_device)
    {
        if (!rhi_device || !rhi_device->GetContextRhi())
            return 0;

        VkPhysicalDeviceMemoryProperties device_memory_properties = {};
        vkGetPhysicalDeviceMemoryProperties(static_cast<VkPhysicalDevice>(rhi_device->GetContextRhi()->device_physical), &device_memory_properties);

        return static_cast<uint32_t>(device_memory_properties.memoryHeaps[0].size / 1024 / 1024); // MBs
    }

    uint32_t RHI_CommandList::Gpu_GetMemoryUsed(RHI_Device* rhi_device)
    {
        if (!rhi_device || !rhi_device->GetContextRhi() || !vulkan_utility::functions::get_physical_device_memory_properties_2)
            return 0;

        VkPhysicalDeviceMemoryBudgetPropertiesEXT device_memory_budget_properties = {};
        device_memory_budget_properties.sType                                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
        device_memory_budget_properties.pNext                                     = nullptr;

        VkPhysicalDeviceMemoryProperties2 device_memory_properties = {};
        device_memory_properties.sType                             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        device_memory_properties.pNext                             = &device_memory_budget_properties;

        vulkan_utility::functions::get_physical_device_memory_properties_2(static_cast<VkPhysicalDevice>(rhi_device->GetContextRhi()->device_physical), &device_memory_properties);

        return static_cast<uint32_t>(device_memory_budget_properties.heapUsage[0] / 1024 / 1024); // MBs
    }

    bool RHI_CommandList::Timestamp_Start(void* query_disjoint /*= nullptr*/, void* query_start /*= nullptr*/) const
    {
        if (!m_rhi_device->GetContextRhi()->profiler)
            return true;

        if (!m_query_pool)
            return false;

        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return false;
        }

        // Reset pool
        vkCmdResetQueryPool(static_cast<VkCommandBuffer>(m_cmd_buffer), static_cast<VkQueryPool>(m_query_pool), 0, 2);

        // Write timestamp
        vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(m_cmd_buffer), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, static_cast<VkQueryPool>(m_query_pool), m_pass_index);

        return true;
    }

    bool RHI_CommandList::Timestamp_End(void* query_disjoint /*= nullptr*/, void* query_end /*= nullptr*/) const
    {
        if (!m_rhi_device->GetContextRhi()->profiler)
            return true;

        if (!m_query_pool)
            return false;

        if (m_cmd_state != RHI_Cmd_List_Recording)
        {
            LOG_WARNING("Can't record command");
            return false;
        }

        // Write timestamp
        vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(m_cmd_buffer), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<VkQueryPool>(m_query_pool), m_pass_index + 1);

        return true;
    }

    float RHI_CommandList::Timestamp_GetDuration(void* query_disjoint /*= nullptr*/, void* query_start /*= nullptr*/, void* query_end /*= nullptr*/)
    {
        if (!m_rhi_device->GetContextRhi()->profiler)
            return true;

        if (!m_query_pool)
            return false;

        const uint32_t query_count    = static_cast<uint32_t>(m_timestamps.size());
        const size_t stride           = sizeof(uint64_t);

        if (vkGetQueryPoolResults(
                m_rhi_device->GetContextRhi()->device,  // device
                static_cast<VkQueryPool>(m_query_pool), // queryPool
                0,                                      // firstQuery
                query_count,                            // queryCount
                query_count * stride,                   // dataSize
                m_timestamps.data(),                    // pData
                stride,                                 // stride
                VK_QUERY_RESULT_64_BIT                  // flags
        ) != VK_SUCCESS) return 0.0f;

        return static_cast<float>((m_timestamps[1] - m_timestamps[0]) * m_rhi_device->GetContextRhi()->device_properties.limits.timestampPeriod * 1e-6f);
    }

    bool RHI_CommandList::Gpu_QueryCreate(RHI_Device* rhi_device, void** query /*= nullptr*/, RHI_Query_Type type /*= RHI_Query_Timestamp*/)
    {
        // Not needed
        return true;
    }

    void RHI_CommandList::Gpu_QueryRelease(void*& query_object)
    {
        // Not needed
    }

    void RHI_CommandList::MarkAndProfileStart(const RHI_PipelineState* pipeline_state)
    {
        if (!pipeline_state || !pipeline_state->pass_name)
            return;

        // Allowed profiler ?
        if (m_rhi_device->GetContextRhi()->profiler)
        {
            if (m_profiler && pipeline_state->profile)
            {
                m_profiler->TimeBlockStart(pipeline_state->pass_name, TimeBlock_Cpu, this);
                m_profiler->TimeBlockStart(pipeline_state->pass_name, TimeBlock_Gpu, this);
            }
        }

        // Allowed to markers ?
        if (m_rhi_device->GetContextRhi()->markers && pipeline_state->mark)
        {
            vulkan_utility::debug::begin(static_cast<VkCommandBuffer>(m_cmd_buffer), pipeline_state->pass_name, Vector4::Zero);
        }

        if (m_pass_index < m_passes_active.size())
        {
            m_passes_active[m_pass_index++] = true;
        }
    }

    void RHI_CommandList::MarkAndProfileEnd(const RHI_PipelineState* pipeline_state)
    {
        if (!pipeline_state || m_pass_index == 0 || !m_passes_active[m_pass_index - 1])
            return;

        m_passes_active[--m_pass_index] = false;

        // Allowed markers ?
        if (m_rhi_device->GetContextRhi()->markers && pipeline_state->mark)
        {
            vulkan_utility::debug::end(static_cast<VkCommandBuffer>(m_cmd_buffer));
        }

        // Allowed profiler ?
        if (m_rhi_device->GetContextRhi()->profiler && pipeline_state->profile)
        {
            if (m_profiler)
            {
                m_profiler->TimeBlockEnd(); // cpu
                m_profiler->TimeBlockEnd(); // gpu
            }
        }
    }

    void RHI_CommandList::BeginRenderPass()
    {
        // Clear values
        array<VkClearValue, state_max_render_target_count + 1> clear_values; // +1 for depth-stencil
        uint32_t clear_value_count = 0;
        {
            // Color
            for (auto i = 0; i < state_max_render_target_count; i++)
            {
                if (m_pipeline_state->clear_color[i] != state_color_load && m_pipeline_state->clear_color[i] != state_depth_dont_care)
                {
                    Vector4& color = m_pipeline_state->clear_color[i];
                    clear_values[clear_value_count++].color = { {color.x, color.y, color.z, color.w} };
                }
            }

            // Depth-stencil
            bool clear_depth    = m_pipeline_state->clear_depth     != state_depth_load     && m_pipeline_state->clear_depth != state_depth_dont_care;
            bool clear_stencil  = m_pipeline_state->clear_stencil   != state_stencil_load   && m_pipeline_state->clear_stencil != state_stencil_dont_care;
            if (clear_depth || clear_stencil)
            {
                clear_values[clear_value_count++].depthStencil = VkClearDepthStencilValue{ m_pipeline_state->clear_depth, m_pipeline_state->clear_stencil };
            }

            // Swapchain
        }

        // Begin render pass
        VkRenderPassBeginInfo render_pass_info      = {};
        render_pass_info.sType                      = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass                 = static_cast<VkRenderPass>(m_pipeline->GetPipelineState()->GetRenderPass());
        render_pass_info.framebuffer                = static_cast<VkFramebuffer>(m_pipeline->GetPipelineState()->GetFrameBuffer());
        render_pass_info.renderArea.offset          = { 0, 0 };
        render_pass_info.renderArea.extent.width    = m_pipeline->GetPipelineState()->GetWidth();
        render_pass_info.renderArea.extent.height   = m_pipeline->GetPipelineState()->GetHeight();
        render_pass_info.clearValueCount            = clear_value_count;
        render_pass_info.pClearValues               = clear_values.data();
        vkCmdBeginRenderPass(static_cast<VkCommandBuffer>(m_cmd_buffer), &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    }

    bool RHI_CommandList::BindDescriptorSet()
    {
        // Descriptor set != null, result = true    -> the descriptor set must be bound
        // Descriptor set == null, result = true    -> the descriptor set is already bound
        // Descriptor set == null, result = false   -> a new descriptor was needed but we are out of memory (allocates next frame)

        void* descriptor_set = nullptr;
        bool result = m_descriptor_cache->GetResource_DescriptorSet(descriptor_set);

        if (result && descriptor_set != nullptr)
        {
            const vector<uint32_t>& _dynamic_offsets    = m_descriptor_cache->GetDynamicOffsets();
            uint32_t dynamic_offset_count               = !_dynamic_offsets.empty() ? static_cast<uint32_t>(_dynamic_offsets.size()) : 0;
            const uint32_t* dynamic_offsets             = !_dynamic_offsets.empty() ? _dynamic_offsets.data() : nullptr;

            // Bind descriptor set
            VkDescriptorSet descriptor_sets[1] = { static_cast<VkDescriptorSet>(descriptor_set) };
            vkCmdBindDescriptorSets
            (
                static_cast<VkCommandBuffer>(m_cmd_buffer),                                                     // commandBuffer
                VK_PIPELINE_BIND_POINT_GRAPHICS,                                // pipelineBindPoint
                static_cast<VkPipelineLayout>(m_pipeline->GetPipelineLayout()), // layout
                0,                                                              // firstSet
                1,                                                              // descriptorSetCount
                descriptor_sets,                                                // pDescriptorSets
                dynamic_offset_count,                                           // dynamicOffsetCount
                dynamic_offsets                                                 // pDynamicOffsets
            );

            m_profiler->m_rhi_bindings_descriptor_set++;

            // Upon setting a new descriptor, resources have to be set again.
            // Note: I could optimize this further and see if the descriptor happens to contain them.
            m_set_id_buffer_vertex  = 0;
            m_set_id_buffer_pixel   = 0;
        }

        return result;
    }

    bool RHI_CommandList::OnDraw()
    {
        // Begin render pass
        if (!m_render_pass_active)
        {
            
            BeginRenderPass();
            m_render_pass_active = true;
        }

        // Set pipeline
        if (!m_pipeline_active)
        {
            // Bind pipeline
            if (VkPipeline vk_pipeline = static_cast<VkPipeline>(m_pipeline->GetPipeline()))
            {
                vkCmdBindPipeline(static_cast<VkCommandBuffer>(m_cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);
                m_profiler->m_rhi_bindings_pipeline++;
                m_pipeline_active = true;
            }
            else
            {
                LOG_ERROR("Invalid pipeline");
                return false;
            }
        }

        // Bind descriptor set
        return BindDescriptorSet();
    }
}
#endif
