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

//= INCLUDES =================
#include <vector>
#include "RHI_Texture.h"
#include "RHI_Viewport.h"
#include "RHI_Definition.h"
#include "../Math/Vector4.h"
#include "../Math/Rectangle.h"
#include "RHI_PipelineCache.h"
//============================

namespace Spartan
{
	class Profiler;

    enum RHI_Cmd_List_State
    {
        RHI_Cmd_List_Idle,
        RHI_Cmd_List_Idle_Sync_Cpu_To_Gpu,
        RHI_Cmd_List_Recording,
        RHI_Cmd_List_Ended
    };

	class SPARTAN_CLASS RHI_CommandList
	{
	public:
		RHI_CommandList(uint32_t index, RHI_SwapChain* swap_chain, Context* context);
		~RHI_CommandList();

        // Passes
        bool Begin(const std::string& pass_name);                                               // Marker
        bool Begin(RHI_PipelineState& pipeline_state);                                          // Pass
        inline bool Begin(const std::string& pass_name, RHI_PipelineState& pipeline_state)      // Pass & Marker
        {
            Begin(pass_name);

            if (!Begin(pipeline_state))
            {
                End(); // end the marker pass
                return false;
            }

            return true;
        }
        bool End();

		// Draw
		void Draw(uint32_t vertex_count);
		void DrawIndexed(uint32_t index_count, uint32_t index_offset = 0, uint32_t vertex_offset = 0);

		// Viewport
		void SetViewport(const RHI_Viewport& viewport);

        // Scissor
		void SetScissorRectangle(const Math::Rectangle& scissor_rectangle);

		// Vertex buffer
		void SetBufferVertex(const RHI_VertexBuffer* buffer);
        inline void SetBufferVertex(const std::shared_ptr<RHI_VertexBuffer>& buffer) { SetBufferVertex(buffer.get()); }

		// Index buffer
		void SetBufferIndex(const RHI_IndexBuffer* buffer);
        inline void SetBufferIndex(const std::shared_ptr<RHI_IndexBuffer>& buffer) { SetBufferIndex(buffer.get()); }

        // Compute shader
        void SetShaderCompute(const RHI_Shader* shader);
        inline void SetShaderCompute(const std::shared_ptr<RHI_Shader>& shader) { SetShaderCompute(shader.get()); }

		// Constant buffer
        void SetConstantBuffer(const uint32_t slot, uint8_t scope, RHI_ConstantBuffer* constant_buffer);
        inline void SetConstantBuffer(const uint32_t slot, uint8_t scope, const std::shared_ptr<RHI_ConstantBuffer>& constant_buffer) { SetConstantBuffer(slot, scope, constant_buffer.get()); }

		// Sampler
        void SetSampler(const uint32_t slot, RHI_Sampler* sampler);
        inline void SetSampler(const uint32_t slot, const std::shared_ptr<RHI_Sampler>& sampler) { SetSampler(slot, sampler.get()); }

		// Texture
        void SetTexture(const uint32_t slot, RHI_Texture* texture);
        inline void SetTexture(const uint32_t slot, const std::shared_ptr<RHI_Texture>& texture) { SetTexture(slot, texture.get()); }
        
		// Render targets
        void ClearRenderTarget(void* render_target, const Math::Vector4& color);
		void ClearDepthStencil(void* depth_stencil, uint32_t flags, float depth, uint8_t stencil = 0);

        // Submit/Flush
		bool Submit();
        bool Flush();

        // GPU - RHI_CommandList instance not required
        static bool Gpu_Flush(RHI_Device* rhi_device);
        static uint32_t Gpu_GetMemory(RHI_Device* rhi_device);
        static uint32_t Gpu_GetMemoryUsed(RHI_Device* rhi_device);
        static bool Gpu_CreateQuery(RHI_Device* rhi_device, void** query, RHI_Query_Type type);
        static bool Gpu_QueryStart(RHI_Device* rhi_device, void* query_object);
        static bool Gpu_GetTimeStamp(RHI_Device* rhi_device, void* query_object);
        static float Gpu_GetDuration(RHI_Device* rhi_device, void* query_disjoint, void* query_start, void* query_end);
        static void Gpu_ReleaseQuery(void* query_object);

        // Misc
        void* GetResource_CommandBuffer() { return m_cmd_buffer; }

	private:
        std::vector<bool> m_passes_active;
        uint32_t m_pass_index                   = 0;
        RHI_Cmd_List_State m_cmd_state          = RHI_Cmd_List_Idle;
		RHI_Pipeline* m_pipeline	            = nullptr; 
        RHI_SwapChain* m_swap_chain             = nullptr;
        Renderer* m_renderer                    = nullptr;
        RHI_PipelineCache* m_rhi_pipeline_cache = nullptr;
        RHI_Device* m_rhi_device                = nullptr;
        Profiler* m_profiler                    = nullptr;
        void* m_cmd_buffer                      = nullptr;
        void* m_cmd_list_consumed_fence         = nullptr;
	};
}
