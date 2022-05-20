/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES =====================
#include <array>
#include <atomic>
#include "RHI_Definition.h"
#include "RHI_PipelineState.h"
#include "RHI_Descriptor.h"
#include "../Core/SpartanObject.h"
#include "../Rendering/Renderer.h"
//================================

namespace Spartan
{
    // Forward declarations
    class Profiler;
    class Context;
    namespace Math
    {
        class Rectangle;
    }

    enum class RHI_CommandListState : uint8_t
    {
        Idle,
        Recording,
        Ended,
        Submitted
    };

    class SPARTAN_CLASS RHI_CommandList : public SpartanObject
    {
    public:
        RHI_CommandList(Context* context);
        ~RHI_CommandList();
    
        // Command list
        void Begin();
        bool End();
        bool Submit(RHI_Semaphore* wait_semaphore);
        bool Reset();
        void Wait();    // Waits for any submitted work
        void Discard(); // Causes the command list to ignore one submission call (useful when the command list refers to resources which have been destroyed).

        // Render pass
        bool BeginRenderPass(RHI_PipelineState& pipeline_state);
        void EndRenderPass();

        // Clear
        void ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state);
        void ClearRenderTarget(
            RHI_Texture* texture,
            const uint32_t color_index = 0,
            const uint32_t depth_stencil_index = 0,
            const bool storage = false,
            const Math::Vector4& clear_color = rhi_color_load,
            const float clear_depth = rhi_depth_stencil_load,
            const float clear_stencil = rhi_depth_stencil_load
        );

        // Draw
        void Draw(uint32_t vertex_count, uint32_t vertex_start_index = 0);
        void DrawIndexed(uint32_t index_count, uint32_t index_offset = 0, uint32_t vertex_offset = 0);

        // Dispatch
        void Dispatch(uint32_t x, uint32_t y, uint32_t z, bool async = false);

        // Blit
        void Blit(RHI_Texture* source, RHI_Texture* destination);
        void Blit(const std::shared_ptr<RHI_Texture>& source, const std::shared_ptr<RHI_Texture>& destination) { Blit(source.get(), destination.get()); }

        // Viewport
        void SetViewport(const RHI_Viewport& viewport) const;
        
        // Scissor
        void SetScissorRectangle(const Math::Rectangle& scissor_rectangle) const;
        
        // Vertex buffer
        void SetBufferVertex(const RHI_VertexBuffer* buffer, const uint64_t offset = 0);
        
        // Index buffer
        void SetBufferIndex(const RHI_IndexBuffer* buffer, const uint64_t offset = 0);

        // Constant buffer
        void SetConstantBuffer(const uint32_t slot, const uint8_t scope, RHI_ConstantBuffer* constant_buffer) const;
        inline void SetConstantBuffer(const Renderer::Bindings_Cb slot, const uint8_t scope, const std::shared_ptr<RHI_ConstantBuffer>& constant_buffer) const { SetConstantBuffer(static_cast<uint32_t>(slot), scope, constant_buffer.get()); }

        // Sampler
        void SetSampler(const uint32_t slot, RHI_Sampler* sampler) const;
        inline void SetSampler(const uint32_t slot, const std::shared_ptr<RHI_Sampler>& sampler) const { SetSampler(slot, sampler.get()); }

        // Texture
        void SetTexture(const uint32_t slot, RHI_Texture* texture, const int mip = -1,bool ranged = false, const bool uav = false);
        inline void SetTexture(const Renderer::Bindings_Uav slot,                        RHI_Texture* texture, const int mip = -1, const bool ranged = false) { SetTexture(static_cast<uint32_t>(slot), texture,       mip, ranged, true); }
        inline void SetTexture(const Renderer::Bindings_Uav slot, const std::shared_ptr<RHI_Texture>& texture, const int mip = -1, const bool ranged = false) { SetTexture(static_cast<uint32_t>(slot), texture.get(), mip, ranged, true); }
        inline void SetTexture(const Renderer::Bindings_Srv slot,                        RHI_Texture* texture, const int mip = -1, const bool ranged = false) { SetTexture(static_cast<uint32_t>(slot), texture,       mip, ranged, false); }
        inline void SetTexture(const Renderer::Bindings_Srv slot, const std::shared_ptr<RHI_Texture>& texture, const int mip = -1, const bool ranged = false) { SetTexture(static_cast<uint32_t>(slot), texture.get(), mip, ranged, false); }

        // Structured buffer
        void SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer) const;
        inline void SetStructuredBuffer(const Renderer::Bindings_Sb slot, const std::shared_ptr<RHI_StructuredBuffer>& structured_buffer) const { SetStructuredBuffer(static_cast<uint32_t>(slot), structured_buffer.get()); }

        // Markers
        void StartMarker(const char* name);
        void EndMarker();

        // Timestamps
        bool Timestamp_Start(void* query);
        bool Timestamp_End(void* query);
        float Timestamp_GetDuration(void* query_start, void* query_end, const uint32_t pass_index);

        // GPU
        static uint32_t Gpu_GetMemory(RHI_Device* rhi_device);
        static uint32_t Gpu_GetMemoryUsed(RHI_Device* rhi_device);

        // Descriptors
        void Descriptors_GetLayoutFromPipelineState(RHI_PipelineState& pipeline_state);
        uint32_t Descriptors_GetDescriptorSetCount() const;
        bool Descriptors_HasEnoughCapacity() const;
        void Descriptors_GetDescriptorsFromPipelineState(RHI_PipelineState& pipeline_state, std::vector<RHI_Descriptor>& descriptors);
        void Descriptors_GrowPool();
        void Descriptors_ResetPool(uint32_t descriptor_set_capacity);
        static void* Descriptors_GetPool() { return m_descriptor_pool; }

        // State
        const RHI_CommandListState GetState() const { return m_state; }

        // Misc
        void* GetResource_CommandBuffer() const { return m_resource; }

    private:    
        void Timeblock_Start(const char* name, const bool profile, const bool gpu_markers);
        void Timeblock_End();

        void OnDraw();
        void UnbindOutputTextures();

        RHI_Pipeline* m_pipeline                         = nullptr; 
        Renderer* m_renderer                             = nullptr;
        RHI_Device* m_rhi_device                         = nullptr;
        Profiler* m_profiler                             = nullptr;
        void* m_resource                                 = nullptr;
        std::shared_ptr<RHI_Fence> m_processed_fence     = nullptr;
        std::atomic<bool> m_discard                      = false;
        bool m_is_render_pass_active                     = false;
        bool m_pipeline_dirty                            = false;
        std::atomic<RHI_CommandListState> m_state        = RHI_CommandListState::Idle;
        static const uint8_t m_resource_array_length_max = 16;
        static bool m_memory_query_support;
        std::mutex m_mutex_reset;

        // Descriptors
        std::unordered_map<std::size_t, std::shared_ptr<RHI_DescriptorSetLayout>> m_descriptor_set_layouts;
        RHI_DescriptorSetLayout* m_descriptor_layout_current = nullptr;
        uint32_t m_descriptor_set_capacity = 0;
        std::atomic<bool> m_descriptor_pool_resseting = false;
        static void* m_descriptor_pool;

        // Pipelines
        RHI_PipelineState m_pipeline_state;
        // <hash of pipeline state, pipeline state object>
        static std::unordered_map<uint32_t, std::shared_ptr<RHI_Pipeline>> m_cache;

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
        void* m_query_pool                     = nullptr;
        uint32_t m_timestamp_index             = 0;
        static const uint32_t m_max_timestamps = 512;
        std::array<uint64_t, m_max_timestamps> m_timestamps;

        // Variables to minimise state changes
        uint64_t m_vertex_buffer_id     = 0;
        uint64_t m_vertex_buffer_offset = 0;
        uint64_t m_index_buffer_id      = 0;
        uint64_t m_index_buffer_offset  = 0;
    };
}
